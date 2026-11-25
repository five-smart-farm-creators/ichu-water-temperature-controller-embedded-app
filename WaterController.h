#ifndef WATER_CONTROLLER_H
#define WATER_CONTROLLER_H

#include "TemperatureRegulator.h"
#include "WaterControllerHardware.h"

struct ControllerUpdate {
    bool hasReading = false;
    TemperatureReading reading;
};

class WaterController {
public:
    WaterController(TemperatureSource& source, HeatingOutput& heater);
    void begin(uint32_t nowMs);
    ControllerUpdate tick(uint32_t nowMs);
    const TemperatureRegulator& regulation() const { return regulation_; }

private:
    TemperatureSource& source_;
    HeatingOutput& heater_;
    TemperatureRegulator regulation_;
};

#endif  // WATER_CONTROLLER_H
