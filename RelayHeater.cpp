#include "RelayHeater.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "WaterControllerConfig.h"

void RelayHeater::begin() {
    // Arduino 3.3.4 ignores digitalWrite before pinMode; preload the latch through the ESP32 driver.
    gpio_set_level(static_cast<gpio_num_t>(WaterControllerConfig::kRelayPin), 0);
    pinMode(WaterControllerConfig::kRelayPin, OUTPUT);
    digitalWrite(WaterControllerConfig::kRelayPin, LOW);
}

void RelayHeater::setEnabled(bool enabled) {
    digitalWrite(WaterControllerConfig::kRelayPin, enabled ? HIGH : LOW);
}
