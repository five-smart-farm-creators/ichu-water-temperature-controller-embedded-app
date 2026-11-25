#include "TemperatureRegulator.h"

#include "WaterControllerConfig.h"

namespace Config = WaterControllerConfig;

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
