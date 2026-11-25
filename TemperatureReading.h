#ifndef TEMPERATURE_READING_H
#define TEMPERATURE_READING_H

#include <math.h>

#include "WaterControllerConfig.h"

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

#endif  // TEMPERATURE_READING_H
