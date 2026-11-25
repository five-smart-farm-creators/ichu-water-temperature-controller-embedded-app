#ifndef WATER_CONTROLLER_HARDWARE_H
#define WATER_CONTROLLER_HARDWARE_H

#include <stdint.h>

#include "TemperatureReading.h"

class TemperatureSource {
public:
    virtual ~TemperatureSource() = default;
    virtual void begin(uint32_t nowMs) = 0;
    // False means no new conversion result; an old temperature must not be replayed.
    virtual bool poll(uint32_t nowMs, TemperatureReading& reading) = 0;
};

class HeatingOutput {
public:
    virtual ~HeatingOutput() = default;
    virtual void begin() = 0;
    virtual void setEnabled(bool enabled) = 0;
};

#endif  // WATER_CONTROLLER_HARDWARE_H
