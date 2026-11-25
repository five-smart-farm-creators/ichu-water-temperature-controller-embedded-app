#ifndef RELAY_HEATER_H
#define RELAY_HEATER_H

#include "WaterControllerHardware.h"

class RelayHeater : public HeatingOutput {
public:
    void begin() override;
    void setEnabled(bool enabled) override;
};

#endif  // RELAY_HEATER_H
