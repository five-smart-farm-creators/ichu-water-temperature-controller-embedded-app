#ifndef SENSOR_TEST_ONE_WIRE_H
#define SENSOR_TEST_ONE_WIRE_H

#include <array>

#include "Arduino.h"

// Protocol-level test double. DallasTemperature itself is the installed, pinned library.
// This model checks driver timing and error handling; it does not simulate electrical signals.
namespace SensorTest {
struct Bus {
    bool present = true;
    bool extraDevice = false;
    bool parasite = false;
    bool stuck = false;
    bool badCrc = false;
    bool badConfiguration = false;
    int16_t temperatureRaw = 22 * 16;
    uint8_t configuration = 0x7f;
    uint64_t conversionStartUs = 0;
    unsigned requests = 0;
    unsigned completionChecks = 0;
    unsigned scratchpadReads = 0;
};
inline Bus bus;
}

class OneWire {
public:
    explicit OneWire(uint8_t) {}

    static uint8_t crc8(const uint8_t* bytes, uint8_t count) {
        uint8_t crc = 0;
        while (count-- != 0) {
            uint8_t value = *bytes++;
            for (unsigned bit = 0; bit < 8; ++bit) {
                const bool mix = ((crc ^ value) & 1) != 0;
                crc >>= 1;
                if (mix) crc ^= 0x8c;
                value >>= 1;
            }
        }
        return crc;
    }

    void reset_search() { searchIndex_ = 0; }
    bool search(uint8_t* address) {
        SensorTest::nowUs += 14000;
        if (!SensorTest::bus.present ||
            searchIndex_ >= (SensorTest::bus.extraDevice ? 2 : 1)) return false;
        const uint8_t rom[7] = {0x28, 1, 2, 3, 4, 5, static_cast<uint8_t>(6 + searchIndex_)};
        for (unsigned index = 0; index < 7; ++index) address[index] = rom[index];
        address[7] = crc8(address, 7);
        ++searchIndex_;
        return true;
    }

    uint8_t reset() {
        SensorTest::nowUs += 960;
        command_ = 0;
        offset_ = 0;
        return SensorTest::bus.present ? 1 : 0;
    }
    void select(const uint8_t*) { SensorTest::nowUs += 9 * 520; }
    void skip() { SensorTest::nowUs += 520; }
    void write(uint8_t value, uint8_t = 0) {
        SensorTest::nowUs += 520;
        if (command_ == 0x4e) {
            if (offset_++ == 2) SensorTest::bus.configuration = value;
            return;
        }
        command_ = value;
        offset_ = 0;
        if (value == 0x44) {
            SensorTest::bus.conversionStartUs = SensorTest::nowUs;
            ++SensorTest::bus.requests;
        } else if (value == 0xbe) {
            ++SensorTest::bus.scratchpadReads;
        }
    }
    uint8_t read_bit() {
        auto& bus = SensorTest::bus;
        uint8_t result = 1;
        if (command_ == 0xb4) {
            result = bus.parasite ? 0 : 1;
        } else if (command_ == 0x44) {
            ++bus.completionChecks;
            result = !bus.stuck && SensorTest::nowUs - bus.conversionStartUs >= 750000 ? 1 : 0;
        }
        SensorTest::nowUs += 66;
        return result;
    }
    void write_bit(uint8_t) { SensorTest::nowUs += 66; }
    uint8_t read() {
        auto& bus = SensorTest::bus;
        const uint16_t raw = static_cast<uint16_t>(bus.temperatureRaw);
        std::array<uint8_t, 9> scratch = {
            static_cast<uint8_t>(raw), static_cast<uint8_t>(raw >> 8), 75, 70,
            static_cast<uint8_t>(bus.badConfiguration ? 0x1f : bus.configuration),
            0xff, 0x0c, 0x10, 0
        };
        scratch[8] = crc8(scratch.data(), 8) ^ (bus.badCrc ? 1 : 0);
        SensorTest::nowUs += 528;
        return scratch[offset_++ % scratch.size()];
    }

private:
    unsigned searchIndex_ = 0;
    uint8_t command_ = 0;
    unsigned offset_ = 0;
};

#endif  // SENSOR_TEST_ONE_WIRE_H
