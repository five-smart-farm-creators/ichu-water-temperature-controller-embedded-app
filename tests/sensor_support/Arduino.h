#ifndef SENSOR_TEST_ARDUINO_H
#define SENSOR_TEST_ARDUINO_H

#include <stddef.h>
#include <stdint.h>

using byte = uint8_t;
constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t OUTPUT = 1;

namespace SensorTest {
inline uint64_t nowUs = 0;
inline unsigned blockingDelays = 0;
}

inline unsigned long millis() { return static_cast<uint32_t>(SensorTest::nowUs / 1000); }
inline unsigned long micros() { return static_cast<uint32_t>(SensorTest::nowUs); }
inline void delay(unsigned long ms) {
    ++SensorTest::blockingDelays;
    SensorTest::nowUs += ms * 1000;
}
inline void yield() { SensorTest::nowUs += 1000; }
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}

template <typename Value, typename Lower, typename Upper>
inline Value constrain(Value value, Lower lower, Upper upper) {
    return value < lower ? static_cast<Value>(lower)
                         : value > upper ? static_cast<Value>(upper) : value;
}

#endif  // SENSOR_TEST_ARDUINO_H
