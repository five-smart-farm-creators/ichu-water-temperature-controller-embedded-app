#include "TemperatureSensor.h"

#include <Arduino.h>

#include "WaterControllerConfig.h"

namespace Config = WaterControllerConfig;

TemperatureSensor::TemperatureSensor() : oneWire_(Config::kTemperaturePin), sensor_(&oneWire_) {}

void TemperatureSensor::begin(uint32_t nowMs) {
    sensor_.setWaitForConversion(false);
    sensor_.setCheckForConversion(true);
    // Keep configuration in the sensor's scratchpad; EEPROM writes would also block for 20 ms.
    sensor_.setAutoSaveScratchPad(false);
    configured_ = false;
    converting_ = false;
    lastStartMs_ = nowMs - Config::kSamplingIntervalMs;
}

SensorError TemperatureSensor::configure() {
    // DallasTemperature 4.0.6 begin() retries with delay(); search directly so recovery never waits.
    oneWire_.reset_search();
    if (!oneWire_.search(address_)) {
        return SensorError::Disconnected;
    }
    uint8_t extraAddress[8] = {};
    if (oneWire_.search(extraAddress) || address_[0] != 0x28 ||
        OneWire::crc8(address_, 7) != address_[7] || sensor_.readPowerSupply(address_)) {
        return SensorError::Configuration;
    }
    if (!sensor_.setResolution(address_, Config::kSensorResolutionBits, true) ||
        sensor_.getResolution(address_) != Config::kSensorResolutionBits) {
        return SensorError::Configuration;
    }
    configured_ = true;
    return SensorError::None;
}

TemperatureReading TemperatureSensor::finishConversion() {
    if (!sensor_.isConversionComplete()) {
        return {0.0f, SensorError::ConversionIncomplete};
    }
    uint8_t scratch[9] = {};
    if (!sensor_.readScratchPad(address_, scratch)) {
        return {0.0f, SensorError::Disconnected};
    }
    bool allZeros = true;
    for (const uint8_t byte : scratch) {
        allZeros = allZeros && byte == 0;
    }
    if (allZeros) {
        return {0.0f, SensorError::Disconnected};
    }
    if (OneWire::crc8(scratch, 8) != scratch[8]) {
        return {0.0f, SensorError::CrcMismatch};
    }
    if ((scratch[4] & 0x60) != 0x60) {
        return {0.0f, SensorError::Configuration};
    }
    // Decode the same CRC-checked scratchpad instead of triggering a second bus read.
    // This also preserves the sensor's -55 C endpoint without a driver's numeric error sentinel.
    const uint16_t bits = static_cast<uint16_t>(scratch[0]) |
                          (static_cast<uint16_t>(scratch[1]) << 8);
    const int32_t raw = (bits & 0x8000) != 0 ? static_cast<int32_t>(bits) - 65536 : bits;
    TemperatureReading reading{static_cast<float>(raw) / 16.0f, SensorError::None};
    reading.error = reading.validatedError();
    return reading;
}

bool TemperatureSensor::poll(uint32_t nowMs, TemperatureReading& reading) {
    if (converting_) {
        const uint32_t elapsed = nowMs - conversionStartMs_;
        // millis() truncates fractions: a 750 ms difference can still be less than 750 ms.
        // Keep the coarse clock for stale detection, including pauses longer than micros() wraps.
        if (elapsed <= Config::kStaleAfterMs &&
            static_cast<uint32_t>(micros() - conversionStartUs_) < Config::kConversionTimeMs * 1000U) {
            return false;
        }
        converting_ = false;
        // A result left unread through a long pause cannot qualify as a fresh sample.
        reading = elapsed > Config::kStaleAfterMs
                      ? TemperatureReading{0.0f, SensorError::Stale}
                      : finishConversion();
        if (reading.error != SensorError::None) {
            configured_ = false;
        }
        return true;
    }
    if (static_cast<uint32_t>(nowMs - lastStartMs_) < Config::kSamplingIntervalMs) {
        return false;
    }
    lastStartMs_ = nowMs;
    if (!configured_) {
        const SensorError error = configure();
        if (error != SensorError::None) {
            reading = {0.0f, error};
            return true;
        }
    }
    const DallasTemperature::request_t request = sensor_.requestTemperaturesByAddress(address_);
    if (!request.result) {
        configured_ = false;
        reading = {0.0f, SensorError::Disconnected};
        return true;
    }
    // Start the precise wait after STARTCONVO has returned; retain the driver's ms sampling cadence.
    conversionStartUs_ = micros();
    conversionStartMs_ = request.timestamp;
    lastStartMs_ = request.timestamp;
    converting_ = true;
    return false;
}
