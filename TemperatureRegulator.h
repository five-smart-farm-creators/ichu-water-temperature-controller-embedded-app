#ifndef TEMPERATURE_REGULATOR_H
#define TEMPERATURE_REGULATOR_H

#include <stdint.h>

#include "TemperatureReading.h"

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

#endif  // TEMPERATURE_REGULATOR_H
