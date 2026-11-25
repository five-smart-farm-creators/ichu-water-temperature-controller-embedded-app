// Composition root: the controller runs locally without network services or persistent state.
#include <Arduino.h>

#include "RelayHeater.h"
#include "TemperatureSensor.h"
#include "WaterController.h"
#include "WaterControllerConfig.h"

static TemperatureSensor temperature;
static RelayHeater heater;
static WaterController controller(temperature, heater);
static uint32_t readingSequence = 0;
static ControlState shownState = ControlState::Qualifying;
static SensorError shownError = SensorError::None;
static bool shownHeater = false;

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
