// Native checks of the controller through simulated sensor and heater interfaces.
#include <limits>
#include <stdio.h>

#define ICHU_NATIVE_TEST
#define ICHU_CONTROL_TEST
#include "../sketch.ino"

namespace {

int checks = 0;
int failures = 0;

void check(bool passed, const char* name) {
    ++checks;
    if (!passed) {
        ++failures;
        printf("FAIL: %s\n", name);
    }
}

class FakeTemperature : public TemperatureSource {
public:
    void begin(uint32_t) override { pending = false; }
    bool poll(uint32_t, TemperatureReading& reading) override {
        if (!pending) {
            return false;
        }
        reading = next;
        pending = false;
        return true;
    }
    void offer(const TemperatureReading& reading) { next = reading; pending = true; }
    TemperatureReading next;
    bool pending = false;
};

class FakeHeater : public HeatingOutput {
public:
    void begin() override { began = true; }
    void setEnabled(bool value) override { enabled = value; }
    bool began = false;
    bool enabled = true;
};

struct Rig {
    FakeTemperature sensor;
    FakeHeater heater;
    WaterController controller{sensor, heater};
    uint32_t now = 0;

    explicit Rig(uint32_t boot = 0) : now(boot) { controller.begin(boot); }
    void reading(float temperature, SensorError error = SensorError::None) {
        now += 1000;
        sensor.offer({temperature, error});
        check(controller.tick(now).hasReading, "offered sample is consumed once");
        check(heater.enabled == controller.regulation().heaterOn(), "output follows regulation");
    }
    void qualify(float temperature) {
        reading(temperature);
        reading(temperature);
        reading(temperature);
    }
    const TemperatureRegulator& state() const { return controller.regulation(); }
};

void startupAndHysteresis() {
    Rig rig;
    check(rig.heater.began && !rig.heater.enabled, "boot initializes output OFF");
    rig.reading(18.0f);
    check(!rig.heater.enabled && rig.state().validReadings() == 1, "first valid sample cannot enable heating");
    rig.reading(18.0f);
    check(!rig.heater.enabled && rig.state().validReadings() == 2, "second valid sample cannot enable heating");
    rig.reading(18.0f);
    check(rig.heater.enabled && rig.state().state() == ControlState::Ready, "third sample at 18 enables heating");
    rig.reading(19.0f);
    check(rig.heater.enabled, "19 preserves ON");
    rig.reading(20.0f);
    check(!rig.heater.enabled, "20 turns heating OFF");
    rig.reading(19.0f);
    check(!rig.heater.enabled, "19 preserves OFF");
    rig.reading(18.0f);
    check(rig.heater.enabled, "18 turns heating ON again");
    rig.controller.begin(rig.now);
    check(!rig.heater.enabled && rig.state().validReadings() == 0, "restart clears heating and qualification");
    rig.reading(18.0f);
    rig.reading(18.0f);
    check(!rig.heater.enabled, "restart needs three new samples");
    rig.reading(18.0f);
    check(rig.heater.enabled, "third post-restart sample enables heating");

    Rig middle;
    middle.qualify(19.0f);
    check(!middle.heater.enabled, "startup at 19 has no stored ON state");
}

void faultsAndRecovery() {
    const SensorError errors[] = {SensorError::Disconnected, SensorError::CrcMismatch,
                                 SensorError::ConversionIncomplete, SensorError::Configuration};
    for (const SensorError error : errors) {
        Rig rig;
        rig.qualify(18.0f);
        rig.reading(18.0f, error);
        check(!rig.heater.enabled && rig.state().validReadings() == 0, "driver error shuts down and clears qualification");
        rig.reading(18.0f);
        check(!rig.heater.enabled, "recovery sample one stays OFF");
        rig.reading(18.0f);
        check(!rig.heater.enabled, "recovery sample two stays OFF");
        rig.reading(0.0f, error);
        check(rig.state().validReadings() == 0, "intermediate error resets recovery");
        rig.qualify(19.0f);
        check(!rig.heater.enabled, "recovery at 19 starts from OFF");
        rig.reading(0.0f, error);
        rig.qualify(18.0f);
        check(rig.heater.enabled, "recovery at 18 resumes after three valid samples");
    }
    const float invalidValues[] = {std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity(), -55.0625f, 125.0625f, -127.0f};
    for (const float value : invalidValues) {
        Rig rig;
        rig.qualify(18.0f);
        rig.reading(value);
        check(!rig.heater.enabled && rig.state().validReadings() == 0, "invalid number is never used as temperature");
        check(rig.state().state() == ControlState::SensorFault, "invalid number exposes a sensor fault");
    }
    Rig lowerEndpoint;
    lowerEndpoint.qualify(-55.0f);
    check(lowerEndpoint.heater.enabled, "sensor lower endpoint is valid; no body-temperature range");
    Rig upperEndpoint;
    upperEndpoint.reading(125.0f);
    check(upperEndpoint.state().overtemperatureLocked(), "sensor upper endpoint is valid and locks heating");
}

void staleReadings() {
    Rig rig;
    rig.qualify(18.0f);
    const uint32_t lastReading = rig.now;
    check(!rig.controller.tick(lastReading + 3000).hasReading, "silence does not reuse an old sample");
    check(rig.heater.enabled, "exactly 3000 ms is not stale");
    rig.controller.tick(lastReading + 3001);
    check(!rig.heater.enabled && rig.state().state() == ControlState::Stale, "more than 3000 ms forces OFF");
    rig.now = lastReading + 3001;
    rig.reading(18.0f);
    rig.reading(18.0f);
    check(!rig.heater.enabled, "stale recovery requires three samples");
    rig.reading(18.0f);
    check(rig.heater.enabled, "third fresh sample recovers from stale");

    Rig gap;
    gap.qualify(18.0f);
    gap.now += 3001;
    gap.sensor.offer({18.0f, SensorError::None});
    gap.controller.tick(gap.now);
    check(!gap.heater.enabled && gap.state().validReadings() == 1, "sample arriving after an unticked stale gap requalifies");

    Rig noSensor;
    noSensor.controller.tick(3001);
    check(!noSensor.heater.enabled && noSensor.state().state() == ControlState::Stale, "no valid sample since boot becomes stale");

    Rig wrap(UINT32_MAX - 1500);
    wrap.qualify(18.0f);
    check(wrap.heater.enabled, "qualification survives millis wraparound");
    wrap.controller.tick(wrap.now + 3001);
    check(!wrap.heater.enabled && wrap.state().state() == ControlState::Stale, "stale subtraction survives wraparound");
}

void overtemperature() {
    Rig rig;
    rig.qualify(18.0f);
    rig.reading(30.0f);
    check(!rig.heater.enabled && rig.state().overtemperatureLocked(), "30 locks heating immediately");
    rig.reading(26.0f);
    check(rig.state().overtemperatureLocked() && rig.state().rearmReadings() == 0, "26 cannot rearm");
    rig.reading(25.0f);
    rig.reading(25.0f);
    check(rig.state().overtemperatureLocked() && rig.state().rearmReadings() == 2, "two readings at 25 keep the lock");
    rig.reading(25.0625f);
    check(rig.state().rearmReadings() == 0, "above 25 resets rearm count");
    rig.reading(25.0f);
    rig.reading(25.0f);
    rig.reading(0.0f, SensorError::CrcMismatch);
    check(rig.state().overtemperatureLocked() && rig.state().rearmReadings() == 0, "invalid sample preserves lock and interrupts rearm");
    rig.reading(25.0f);
    rig.reading(25.0f);
    check(!rig.heater.enabled && rig.state().overtemperatureLocked(), "fault recovery cannot bypass overtemperature lock");
    rig.reading(25.0f);
    check(!rig.heater.enabled && !rig.state().overtemperatureLocked(), "third valid 25 rearms and stays OFF");
    rig.reading(18.0f);
    check(rig.heater.enabled, "normal regulation resumes after rearm");
    rig.reading(85.0f);
    check(rig.state().overtemperatureLocked() && rig.state().error() == SensorError::None,
          "completed valid 85 C is overtemperature, not a sensor error");
    rig.reading(18.0f);
    rig.reading(18.0f);
    check(!rig.heater.enabled, "normal ON requests cannot bypass rearm count");
    rig.reading(18.0f);
    check(rig.heater.enabled && !rig.state().overtemperatureLocked(), "third cool sample rearms then applies current 18 C");
    rig.reading(30.0f);
    rig.reading(25.0f);
    rig.reading(25.0f);
    rig.controller.tick(rig.now + 3001);
    check(rig.state().overtemperatureLocked() && rig.state().rearmReadings() == 0, "stale data interrupts overtemperature rearm");
    rig.now += 3001;
    rig.controller.begin(rig.now);
    check(!rig.heater.enabled && !rig.state().overtemperatureLocked(), "restart clears RAM lock and starts OFF");
    rig.reading(30.0f);
    check(!rig.heater.enabled && rig.state().overtemperatureLocked(), "new hot reading relocks during startup");
}

}  // namespace

int main() {
    startupAndHysteresis();
    faultsAndRecovery();
    staleReadings();
    overtemperature();
    printf("%d controller checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
