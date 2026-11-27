// Single-file water controller using Modest IoT Nano Framework 1.3.0.
// Cooperative Sensor events and Actuator commands preserve the existing loop timing.
// The framework's Device network/sampler engine is intentionally not started.
#include <Arduino.h>
#include <Actuator.h>
#include <Sensor.h>
#include <math.h>
#include <stdint.h>

#if !defined(ICHU_CONTROL_TEST)
#include <DallasTemperature.h>
#include <OneWire.h>
#endif
#if !defined(ICHU_NATIVE_TEST)
#include <driver/gpio.h>
#endif

// Simulation configuration
// User-approved Wokwi demonstration values, not physical installation parameters.
namespace WaterControllerConfig {

constexpr uint8_t kTemperaturePin = 4;
constexpr uint8_t kRelayPin = 26;
constexpr float kHeatingOnCelsius = 18.0f;
constexpr float kHeatingOffCelsius = 20.0f;
constexpr float kOvertemperatureCelsius = 30.0f;
constexpr float kRearmCelsius = 25.0f;
constexpr uint8_t kRequiredValidReadings = 3;
constexpr uint8_t kRequiredRearmReadings = 3;
constexpr uint32_t kSamplingIntervalMs = 1000;
constexpr uint32_t kStaleAfterMs = 3000;
constexpr uint8_t kSensorResolutionBits = 12;
// DS18B20 maximum conversion time at 12 bits, not a product sampling interval.
constexpr uint32_t kConversionTimeMs = 750;
constexpr float kSensorMinimumCelsius = -55.0f;
constexpr float kSensorMaximumCelsius = 125.0f;

static_assert(kHeatingOnCelsius < kHeatingOffCelsius, "Heating thresholds must be ordered");
static_assert(kRearmCelsius < kOvertemperatureCelsius, "Rearm must be below the lock threshold");
static_assert(kConversionTimeMs < kSamplingIntervalMs, "Each conversion must finish before the next sample");
static_assert(kRequiredValidReadings > 0 && kRequiredRearmReadings > 0, "Qualification cannot be empty");

}  // namespace WaterControllerConfig

namespace Config = WaterControllerConfig;

// Reading validation and framework sensor/actuator adapters
enum class SensorError {
    None,
    Disconnected,
    CrcMismatch,
    ConversionIncomplete,
    Configuration,
    InvalidValue,
    OutOfRange,
    Stale
};

struct TemperatureReading {
    float celsius = 0.0f;
    SensorError error = SensorError::None;

    SensorError validatedError() const {
        if (error != SensorError::None) {
            return error;
        }
        if (!isfinite(celsius)) {
            return SensorError::InvalidValue;
        }
        if (celsius < WaterControllerConfig::kSensorMinimumCelsius ||
            celsius > WaterControllerConfig::kSensorMaximumCelsius) {
            return SensorError::OutOfRange;
        }
        return SensorError::None;
    }
};

class TemperatureSource : public Sensor {
public:
    TemperatureSource() : Sensor(WaterControllerConfig::kTemperaturePin) {}
    ~TemperatureSource() override = default;

    // Sensor::on dispatches completed readings to the controller synchronously.
    bool sample(uint32_t nowMs, TemperatureReading& reading) {
        sampleTimeMs_ = nowMs;
        hasReading_ = false;
        on(Event(MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER));
        if (hasReading_) {
            reading = latestReading_;
        }
        return hasReading_;
    }
    const TemperatureReading& latestReading() const { return latestReading_; }
    uint32_t sampleTimeMs() const { return sampleTimeMs_; }
    virtual void begin(uint32_t nowMs) = 0;
    // False means no new conversion result; an old temperature must not be replayed.
    virtual bool poll(uint32_t nowMs, TemperatureReading& reading) = 0;

protected:
    void processEvent(Event& event) override {
        if (event.identifier != MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) {
            event.identifier = -1;
            return;
        }
        hasReading_ = poll(sampleTimeMs_, latestReading_);
        event.identifier = hasReading_ ? DATA_READ_EVENT_IDENTIFIER : -1;
        event.sourceId = getPin();
    }
    uint32_t getDefaultSamplingIntervalInMilliseconds() const override {
        return WaterControllerConfig::kSamplingIntervalMs;
    }
    uint32_t getMinimalSamplingIntervalInMilliseconds() const override {
        return WaterControllerConfig::kSamplingIntervalMs;
    }

private:
    TemperatureReading latestReading_;
    uint32_t sampleTimeMs_ = 0;
    bool hasReading_ = false;
};

class HeatingOutput : public Actuator {
public:
    HeatingOutput() : Actuator(WaterControllerConfig::kRelayPin) {}
    ~HeatingOutput() override = default;
    void requestEnabled(bool enabled) { handle(Command(kSetEnabledCommand, enabled ? 1 : 0)); }
    virtual void begin() = 0;
    virtual void setEnabled(bool enabled) = 0;

private:
    static constexpr int kSetEnabledCommand = 1000;
    void executeCommand(Command command) override {
        if (command.identifier == kSetEnabledCommand) {
            setEnabled(command.payload != 0);
        }
    }
};

// Temperature regulation: existing thresholds, qualification and fault rules
enum class ControlState { Qualifying, Ready, SensorFault, Stale, Overtemperature };

// Pure control rules; neither Arduino nor a network service is needed here.
class TemperatureRegulator {
public:
    void reset(uint32_t nowMs);
    void tick(uint32_t nowMs);
    void observe(const TemperatureReading& reading, uint32_t nowMs);

    bool heaterOn() const { return heaterOn_; }
    bool overtemperatureLocked() const { return locked_; }
    uint8_t validReadings() const { return validReadings_; }
    uint8_t rearmReadings() const { return rearmReadings_; }
    SensorError error() const { return error_; }
    ControlState state() const;

private:
    void fail(SensorError error);

    uint32_t lastValidMs_ = 0;
    uint8_t validReadings_ = 0;
    uint8_t rearmReadings_ = 0;
    bool heaterOn_ = false;
    bool locked_ = false;
    SensorError error_ = SensorError::None;
};

void TemperatureRegulator::reset(uint32_t nowMs) {
    lastValidMs_ = nowMs;
    validReadings_ = 0;
    rearmReadings_ = 0;
    heaterOn_ = false;
    locked_ = false;
    error_ = SensorError::None;
}

void TemperatureRegulator::fail(SensorError error) {
    heaterOn_ = false;
    validReadings_ = 0;
    rearmReadings_ = 0;
    error_ = error;
    // A failed sensor cannot clear an existing overtemperature lock.
}

void TemperatureRegulator::tick(uint32_t nowMs) {
    if (static_cast<uint32_t>(nowMs - lastValidMs_) > Config::kStaleAfterMs) {
        fail(SensorError::Stale);
    }
}

void TemperatureRegulator::observe(const TemperatureReading& reading, uint32_t nowMs) {
    // Qualification must restart even if the first tick after a long gap also brings a reading.
    tick(nowMs);
    const SensorError error = reading.validatedError();
    if (error != SensorError::None) {
        fail(error);
        return;
    }
    lastValidMs_ = nowMs;
    error_ = SensorError::None;
    if (validReadings_ < Config::kRequiredValidReadings) {
        ++validReadings_;
    }

    if (reading.celsius >= Config::kOvertemperatureCelsius) {
        locked_ = true;
        rearmReadings_ = 0;
    }
    if (locked_) {
        heaterOn_ = false;
        if (reading.celsius <= Config::kRearmCelsius) {
            if (rearmReadings_ < Config::kRequiredRearmReadings) {
                ++rearmReadings_;
            }
        } else {
            rearmReadings_ = 0;
        }
        if (rearmReadings_ < Config::kRequiredRearmReadings) {
            return;
        }
        locked_ = false;
        rearmReadings_ = 0;
    }

    if (validReadings_ < Config::kRequiredValidReadings) {
        heaterOn_ = false;
        return;
    }
    if (reading.celsius <= Config::kHeatingOnCelsius) {
        heaterOn_ = true;
    } else if (reading.celsius >= Config::kHeatingOffCelsius) {
        heaterOn_ = false;
    }
}

ControlState TemperatureRegulator::state() const {
    if (error_ == SensorError::Stale) {
        return ControlState::Stale;
    }
    if (error_ != SensorError::None) {
        return ControlState::SensorFault;
    }
    if (locked_) {
        return ControlState::Overtemperature;
    }
    return validReadings_ < Config::kRequiredValidReadings ? ControlState::Qualifying : ControlState::Ready;
}

// Sensor-event mediator and heater command dispatch
struct ControllerUpdate {
    bool hasReading = false;
    TemperatureReading reading;
};

class WaterController : public EventHandler {
public:
    WaterController(TemperatureSource& source, HeatingOutput& heater);
    void begin(uint32_t nowMs);
    ControllerUpdate tick(uint32_t nowMs);
    void on(Event event) override;
    const TemperatureRegulator& regulation() const { return regulation_; }

private:
    TemperatureSource& source_;
    HeatingOutput& heater_;
    TemperatureRegulator regulation_;
};

WaterController::WaterController(TemperatureSource& source, HeatingOutput& heater)
    : source_(source), heater_(heater) {
    source_.setHandler(this);
}

void WaterController::begin(uint32_t nowMs) {
    heater_.begin();
    heater_.requestEnabled(false);
    regulation_.reset(nowMs);
    source_.begin(nowMs);
}

ControllerUpdate WaterController::tick(uint32_t nowMs) {
    regulation_.tick(nowMs);
    // Apply stale shutdown before touching the sensor bus.
    heater_.requestEnabled(regulation_.heaterOn());
    ControllerUpdate update;
    update.hasReading = source_.sample(nowMs, update.reading);
    heater_.requestEnabled(regulation_.heaterOn());
    return update;
}

void WaterController::on(Event event) {
    if (event.identifier == Sensor::DATA_READ_EVENT_IDENTIFIER &&
        event.sourceId == WaterControllerConfig::kTemperaturePin) {
        regulation_.observe(source_.latestReading(), source_.sampleTimeMs());
    }
}

#if !defined(ICHU_CONTROL_TEST)
// Non-blocking DS18B20 driver
class TemperatureSensor : public TemperatureSource {
public:
    TemperatureSensor();
    void begin(uint32_t nowMs) override;
    bool poll(uint32_t nowMs, TemperatureReading& reading) override;

private:
    SensorError configure();
    TemperatureReading finishConversion();

    OneWire oneWire_;
    DallasTemperature sensor_;
    DeviceAddress address_ = {};
    uint32_t lastStartMs_ = 0;
    uint32_t conversionStartMs_ = 0;
    uint32_t conversionStartUs_ = 0;
    bool configured_ = false;
    bool converting_ = false;
};

TemperatureSensor::TemperatureSensor() : oneWire_(Config::kTemperaturePin), sensor_(&oneWire_) {}

void TemperatureSensor::begin(uint32_t nowMs) {
    sensor_.setWaitForConversion(false);
    sensor_.setCheckForConversion(true);
    // Keep configuration in the sensor's scratchpad; EEPROM writes would also block for 20 ms.
    sensor_.setAutoSaveScratchPad(false);
    configured_ = false;
    converting_ = false;
    lastStartMs_ = nowMs - Config::kSamplingIntervalMs;
}

SensorError TemperatureSensor::configure() {
    // DallasTemperature 4.0.6 begin() retries with delay(); search directly so recovery never waits.
    oneWire_.reset_search();
    if (!oneWire_.search(address_)) {
        return SensorError::Disconnected;
    }
    uint8_t extraAddress[8] = {};
    if (oneWire_.search(extraAddress) || address_[0] != 0x28 ||
        OneWire::crc8(address_, 7) != address_[7] || sensor_.readPowerSupply(address_)) {
        return SensorError::Configuration;
    }
    if (!sensor_.setResolution(address_, Config::kSensorResolutionBits, true) ||
        sensor_.getResolution(address_) != Config::kSensorResolutionBits) {
        return SensorError::Configuration;
    }
    configured_ = true;
    return SensorError::None;
}

TemperatureReading TemperatureSensor::finishConversion() {
    if (!sensor_.isConversionComplete()) {
        return {0.0f, SensorError::ConversionIncomplete};
    }
    uint8_t scratch[9] = {};
    if (!sensor_.readScratchPad(address_, scratch)) {
        return {0.0f, SensorError::Disconnected};
    }
    bool allZeros = true;
    for (const uint8_t byte : scratch) {
        allZeros = allZeros && byte == 0;
    }
    if (allZeros) {
        return {0.0f, SensorError::Disconnected};
    }
    if (OneWire::crc8(scratch, 8) != scratch[8]) {
        return {0.0f, SensorError::CrcMismatch};
    }
    if ((scratch[4] & 0x60) != 0x60) {
        return {0.0f, SensorError::Configuration};
    }
    // Decode the same CRC-checked scratchpad instead of triggering a second bus read.
    // This also preserves the sensor's -55 C endpoint without a driver's numeric error sentinel.
    const uint16_t bits = static_cast<uint16_t>(scratch[0]) |
                          (static_cast<uint16_t>(scratch[1]) << 8);
    const int32_t raw = (bits & 0x8000) != 0 ? static_cast<int32_t>(bits) - 65536 : bits;
    TemperatureReading reading{static_cast<float>(raw) / 16.0f, SensorError::None};
    reading.error = reading.validatedError();
    return reading;
}

bool TemperatureSensor::poll(uint32_t nowMs, TemperatureReading& reading) {
    if (converting_) {
        const uint32_t elapsed = nowMs - conversionStartMs_;
        // millis() truncates fractions: a 750 ms difference can still be less than 750 ms.
        // Keep the coarse clock for stale detection, including pauses longer than micros() wraps.
        if (elapsed <= Config::kStaleAfterMs &&
            static_cast<uint32_t>(micros() - conversionStartUs_) < Config::kConversionTimeMs * 1000U) {
            return false;
        }
        converting_ = false;
        // A result left unread through a long pause cannot qualify as a fresh sample.
        reading = elapsed > Config::kStaleAfterMs
                      ? TemperatureReading{0.0f, SensorError::Stale}
                      : finishConversion();
        if (reading.error != SensorError::None) {
            configured_ = false;
        }
        return true;
    }
    if (static_cast<uint32_t>(nowMs - lastStartMs_) < Config::kSamplingIntervalMs) {
        return false;
    }
    lastStartMs_ = nowMs;
    if (!configured_) {
        const SensorError error = configure();
        if (error != SensorError::None) {
            reading = {0.0f, error};
            return true;
        }
    }
    const DallasTemperature::request_t request = sensor_.requestTemperaturesByAddress(address_);
    if (!request.result) {
        configured_ = false;
        reading = {0.0f, SensorError::Disconnected};
        return true;
    }
    // Start the precise wait after STARTCONVO has returned; retain the driver's ms sampling cadence.
    conversionStartUs_ = micros();
    conversionStartMs_ = request.timestamp;
    lastStartMs_ = request.timestamp;
    converting_ = true;
    return false;
}
#endif

#if !defined(ICHU_NATIVE_TEST)
// ESP32 relay and Arduino entry points
class RelayHeater : public HeatingOutput {
public:
    void begin() override;
    void setEnabled(bool enabled) override;
};

void RelayHeater::begin() {
    // Arduino 3.3.4 ignores digitalWrite before pinMode; preload the latch through the ESP32 driver.
    gpio_set_level(static_cast<gpio_num_t>(WaterControllerConfig::kRelayPin), 0);
    pinMode(WaterControllerConfig::kRelayPin, OUTPUT);
    digitalWrite(WaterControllerConfig::kRelayPin, LOW);
}

void RelayHeater::setEnabled(bool enabled) {
    digitalWrite(WaterControllerConfig::kRelayPin, enabled ? HIGH : LOW);
}



static TemperatureSensor temperature;
static RelayHeater heater;
static WaterController controller(temperature, heater);
static uint32_t readingSequence = 0;
static ControlState shownState = ControlState::Qualifying;
static SensorError shownError = SensorError::None;
static bool shownHeater = false;

// Explicit declarations keep Arduino's generated prototypes after the application types.
const char* stateName(ControlState state);
const char* errorName(SensorError error);
void printState(const TemperatureRegulator& regulation);

const char* stateName(ControlState state) {
    switch (state) {
        case ControlState::Qualifying: return "QUALIFYING";
        case ControlState::Ready: return "READY";
        case ControlState::SensorFault: return "SENSOR_FAULT";
        case ControlState::Stale: return "STALE";
        case ControlState::Overtemperature: return "OVERTEMPERATURE";
    }
    return "UNKNOWN";
}

const char* errorName(SensorError error) {
    switch (error) {
        case SensorError::None: return "none";
        case SensorError::Disconnected: return "disconnected";
        case SensorError::CrcMismatch: return "crc";
        case SensorError::ConversionIncomplete: return "conversion-incomplete";
        case SensorError::Configuration: return "configuration";
        case SensorError::InvalidValue: return "non-finite";
        case SensorError::OutOfRange: return "sensor-range";
        case SensorError::Stale: return "stale";
    }
    return "unknown";
}

void printState(const TemperatureRegulator& regulation) {
    Serial.printf(" heater=%s state=%s valid=%u/%u rearm=%u/%u locked=%u error=%s\n",
                  regulation.heaterOn() ? "ON" : "OFF", stateName(regulation.state()),
                  static_cast<unsigned>(regulation.validReadings()),
                  static_cast<unsigned>(WaterControllerConfig::kRequiredValidReadings),
                  static_cast<unsigned>(regulation.rearmReadings()),
                  static_cast<unsigned>(WaterControllerConfig::kRequiredRearmReadings),
                  static_cast<unsigned>(regulation.overtemperatureLocked()), errorName(regulation.error()));
}

void setup() {
    // Drive the output LOW before initializing Serial or touching the sensor bus.
    controller.begin(millis());
    Serial.begin(115200);
    Serial.println("[boot] ICHU water controller - Wokwi demonstration; heater=OFF");
    printState(controller.regulation());
}

void loop() {
    const ControllerUpdate update = controller.tick(millis());
    const TemperatureRegulator& regulation = controller.regulation();
    if (update.hasReading) {
        ++readingSequence;
        Serial.printf("[reading] seq=%lu", static_cast<unsigned long>(readingSequence));
        if (update.reading.validatedError() == SensorError::None) {
            Serial.printf(" temp=%.2fC", update.reading.celsius);
        } else {
            Serial.print(" temp=invalid");
        }
        printState(regulation);
    } else if (shownState != regulation.state() || shownError != regulation.error() ||
               shownHeater != regulation.heaterOn()) {
        Serial.print("[state]");
        printState(regulation);
    }
    shownState = regulation.state();
    shownError = regulation.error();
    shownHeater = regulation.heaterOn();
    yield();
}
#endif
