#ifndef WATER_CONTROLLER_CONFIG_H
#define WATER_CONTROLLER_CONFIG_H

#include <stdint.h>

// User-approved Wokwi demonstration values, not physical installation parameters.
namespace WaterControllerConfig {

constexpr uint8_t kTemperaturePin = 4;
constexpr uint8_t kRelayPin = 26;
constexpr float kHeatingOnCelsius = 18.0f;
constexpr float kHeatingOffCelsius = 20.0f;
constexpr float kOvertemperatureCelsius = 30.0f;
constexpr float kRearmCelsius = 25.0f;
constexpr uint8_t kRequiredValidReadings = 3;
constexpr uint8_t kRequiredRearmReadings = 3;
constexpr uint32_t kSamplingIntervalMs = 1000;
constexpr uint32_t kStaleAfterMs = 3000;
constexpr uint8_t kSensorResolutionBits = 12;
// DS18B20 maximum conversion time at 12 bits, not a product sampling interval.
constexpr uint32_t kConversionTimeMs = 750;
constexpr float kSensorMinimumCelsius = -55.0f;
constexpr float kSensorMaximumCelsius = 125.0f;

static_assert(kHeatingOnCelsius < kHeatingOffCelsius, "Heating thresholds must be ordered");
static_assert(kRearmCelsius < kOvertemperatureCelsius, "Rearm must be below the lock threshold");
static_assert(kConversionTimeMs < kSamplingIntervalMs, "Each conversion must finish before the next sample");
static_assert(kRequiredValidReadings > 0 && kRequiredRearmReadings > 0, "Qualification cannot be empty");

}  // namespace WaterControllerConfig

#endif  // WATER_CONTROLLER_CONFIG_H
