#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <DallasTemperature.h>
#include <OneWire.h>

#include "WaterControllerHardware.h"

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

#endif  // TEMPERATURE_SENSOR_H
