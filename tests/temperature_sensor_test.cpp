// Compile the real sensor adapter AND DallasTemperature.cpp against a timed OneWire test bus.
#include <stdio.h>

#define ICHU_NATIVE_TEST
#include "../sketch.ino"

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        ++failures;
        printf("FAIL: %s\n", name);
    }
}

struct Rig {
    TemperatureSensor sensor;
    TemperatureReading reading;

    explicit Rig(uint64_t bootUs = 123) {
        SensorTest::nowUs = bootUs;
        SensorTest::blockingDelays = 0;
        SensorTest::bus = {};
        sensor.begin(millis());
    }
    bool poll() { return sensor.sample(millis(), reading); }
    void start() {
        check(!poll(), "request starts asynchronously without returning cached temperature");
        check(SensorTest::bus.requests == 1, "one addressed conversion is started");
    }
    void finish() { SensorTest::nowUs = SensorTest::bus.conversionStartUs + 750000; }
};

void fractionalMillisecond() {
    Rig rig;
    rig.start();
    const uint64_t startedUs = SensorTest::bus.conversionStartUs;
    check(startedUs % 1000 != 0, "conversion begins between millisecond boundaries");
    SensorTest::nowUs = (startedUs / 1000 + 750) * 1000;
    printf("Regression boundary: elapsed millis=750, actual elapsed us=%llu\n",
           static_cast<unsigned long long>(SensorTest::nowUs - startedUs));
    check(!rig.poll(), "750 rounded milliseconds must not publish an incomplete conversion");
    check(SensorTest::bus.completionChecks == 0, "do not query completion before 750000 us");
    rig.finish();
    check(rig.poll() && rig.reading.error == SensorError::None && rig.reading.celsius == 22.0f,
          "the same conversion yields valid 22 C after the full duration");
    check(!rig.poll(), "a completed conversion is consumed only once");
    check(SensorTest::bus.requests == 1, "no second request before the sampling interval");
    SensorTest::nowUs = (startedUs / 1000 + 1000) * 1000;
    check(!rig.poll() && SensorTest::bus.requests == 2, "next conversion starts at the 1000 ms interval");
    check(SensorTest::blockingDelays == 0, "initialization, conversion and recovery never call delay");
}

void completionAndValidation() {
    {
        Rig rig;
        rig.start();
        SensorTest::bus.stuck = true;
        rig.finish();
        check(rig.poll() && rig.reading.error == SensorError::ConversionIncomplete,
              "a genuinely incomplete conversion is still rejected");
    }
    {
        Rig rig;
        rig.start();
        SensorTest::bus.present = false;
        rig.finish();
        check(rig.poll() && rig.reading.error == SensorError::Disconnected,
              "scratchpad presence is still checked after conversion");
    }
    {
        Rig rig;
        rig.start();
        SensorTest::bus.badCrc = true;
        rig.finish();
        check(rig.poll() && rig.reading.error == SensorError::CrcMismatch,
              "scratchpad CRC is still required");
    }
    {
        Rig rig;
        rig.start();
        SensorTest::bus.badConfiguration = true;
        rig.finish();
        check(rig.poll() && rig.reading.error == SensorError::Configuration,
              "a result at another resolution is still rejected");
    }
    for (const int16_t raw : {-55 * 16, 85 * 16, 125 * 16, 125 * 16 + 1}) {
        Rig rig;
        rig.start();
        SensorTest::bus.temperatureRaw = raw;
        rig.finish();
        const SensorError expected = raw > 125 * 16 ? SensorError::OutOfRange : SensorError::None;
        check(rig.poll() && rig.reading.error == expected, "range validation and valid 85 C are preserved");
    }
}

void clockWrapAndStaleness() {
    for (const uint64_t bootUs : {(uint64_t{1} << 32) - 400000,
                                 (uint64_t{UINT32_MAX} - 300) * 1000 + 123}) {
        Rig rig(bootUs);
        rig.start();
        rig.finish();
        check(rig.poll() && rig.reading.error == SensorError::None,
              "conversion timing survives micros and millis wraparound");
    }
    for (const uint64_t pauseUs : {uint64_t{3001000}, (uint64_t{1} << 32) + 100000}) {
        Rig rig;
        rig.start();
        SensorTest::nowUs = SensorTest::bus.conversionStartUs + pauseUs;
        check(rig.poll() && rig.reading.error == SensorError::Stale,
              "a long pause cannot turn an old conversion into a fresh sample");
        check(SensorTest::bus.completionChecks == 0, "stale result is discarded before querying the bus");
    }
}
}

int main() {
    fractionalMillisecond();
    completionAndValidation();
    clockWrapAndStaleness();
    printf("%d sensor driver checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
