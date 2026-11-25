#include "WaterController.h"

WaterController::WaterController(TemperatureSource& source, HeatingOutput& heater)
    : source_(source), heater_(heater) {}

void WaterController::begin(uint32_t nowMs) {
    heater_.begin();
    heater_.setEnabled(false);
    regulation_.reset(nowMs);
    source_.begin(nowMs);
}

ControllerUpdate WaterController::tick(uint32_t nowMs) {
    regulation_.tick(nowMs);
    // Apply stale shutdown before touching the sensor bus.
    heater_.setEnabled(regulation_.heaterOn());
    ControllerUpdate update;
    update.hasReading = source_.poll(nowMs, update.reading);
    if (update.hasReading) {
        regulation_.observe(update.reading, nowMs);
    }
    heater_.setEnabled(regulation_.heaterOn());
    return update;
}
