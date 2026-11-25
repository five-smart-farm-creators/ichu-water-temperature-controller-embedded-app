# ICHU Water Temperature Controller Embedded Application

Local ESP32 firmware for a **Wokwi demonstration** of water-temperature regulation. It reads one simulated DS18B20, controls a relay and reports measurements, qualification, faults and overtemperature state at 115200 baud. A red LED powered through the relay contacts indicates the requested heating state. It does not model heat transfer.

All components, connections and numeric settings below were explicitly selected for this simulation. They are not physical specifications established by the ICHU report, livestock drinking-water recommendations or validated installation parameters. The report's conceptual circuit does not verify the physical sensor or heater model. Its resistor marked 200 ohms is not used as a sensor specification here: this simulation uses the explicitly approved **4.7 kohm pull-up**.

No Wi-Fi, Internet, HTTP, MQTT, credentials, Edge or backend connection is initialized. Water level, filling, pumps and valves are outside this application. Persistent buffering and cloud integration are deferred; there are no placeholder implementations or per-reading flash writes. Restarting discards the RAM state.

## Structure

Sources stay at the root, as in the collar and alert-panel repositories:

| File | Responsibility |
| --- | --- |
| `sketch.ino` | Composition root, non-blocking loop and serial observations |
| `WaterControllerConfig.h` | Simulation pins, thresholds, qualification and timing as `constexpr` |
| `WaterController.h/.cpp` | Coordinates fresh readings, regulation and output |
| `TemperatureRegulator.h/.cpp` | Hardware-independent rules and RAM state |
| `TemperatureReading.h` | Explicit errors and finite/sensor-range validation |
| `WaterControllerHardware.h` | Sensor and output interfaces used by hardware and native test doubles |
| `TemperatureSensor.h/.cpp` | Asynchronous 1-Wire conversion and checked scratchpad reading |
| `RelayHeater.h/.cpp` | Active-HIGH GPIO26 output; LOW is written before output initialization |
| `diagram.json`, `wokwi.toml`, `libraries.txt` | Circuit, generated firmware paths and pinned dependencies |
| `tests/` | Native controller tests and Wokwi automation scenarios |

The reference firmwares use Modest IoT Nano Framework. This local-only version uses a small `millis()` scheduler instead of its `Device` sampler/network engine, so no network task is introduced. Hardware drivers and regulation remain separate, without a dependency on a future cloud adapter.
