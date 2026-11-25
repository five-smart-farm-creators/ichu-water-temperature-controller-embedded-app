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

## Simulation wiring

| Component pin or connection | Destination |
| --- | --- |
| ESP32 board | `board-esp32-devkit-c-v4`; FQBN `esp32:esp32:esp32` |
| DS18B20 DQ | GPIO4 |
| DS18B20 VCC / GND | 3V3 / common GND; three-wire supply, no parasite power |
| 4.7 kohm pull-up | GPIO4/DQ to 3V3 |
| Relay IN / VCC / GND | GPIO26 / simulated board 5V / common GND |
| Relay COM | Simulated board 5V |
| Relay NO | 330 ohm resistor, then red LED anode A |
| Red LED cathode C | Common GND |
| Relay NC | Unconnected |

The load label is **Heater indicator — simulation**. It is supplied only through the relay contacts; no second GPIO drives it. Sensor, ESP32 and relay/load are arranged from left to right. The initial simulated temperature is 22 C. No mains load is represented.

### Relay verification is outstanding

The requested behavior is HIGH closes COM-NO and lights the load; LOW opens COM-NO and extinguishes it. The circuit initially uses `transistor="npn"` as authorized. The [Wokwi relay reference](https://docs.wokwi.com/parts/wokwi-relay-module) contradicts itself between the attribute table and operation text. This configuration has **not yet been verified in a running simulation**. The firmware polarity has not been inverted to hide that discrepancy.

`tests/hysteresis-relay.test.yaml` asserts GPIO26, COM, NO and the actual LED load pins. A GPIO or serial assertion alone is insufficient. The load-side assertions and visual LED observation must pass before claiming correct relay behavior. If they fail, investigate the supported `transistor` configuration, record the evidence and preserve the requested HIGH/LOW behavior. A floating output or unsupported pin assertion must not be counted as a pass. Native tests do not establish contact polarity or LED current.

## Demonstration control rules

Change the `constexpr` settings in `WaterControllerConfig.h`, then rebuild the firmware. These values are the authorized demonstration profile:

| Setting | Value |
| --- | --- |
| Heating ON | Valid temperature <= 18.0 C |
| Heating OFF | Valid temperature >= 20.0 C |
| Between 18 and 20 C | Preserve the previous state |
| Startup and fault recovery | Three consecutive valid readings, with output OFF until the third |
| Conversion start interval | 1000 ms; first conversion starts as soon as initialization permits |
| DS18B20 resolution / conversion wait | 12 bits / at least 750 ms after the driver's STARTCONVO timestamp |
| Stale reading | More than 3000 ms without a completed valid reading forces OFF and restarts qualification |
| Overtemperature lock | Any valid temperature >= 30.0 C forces OFF, including during qualification |
| Automatic rearm | Three consecutive valid readings <= 25.0 C |

An invalid reading resets both counters, forces OFF and preserves an existing overtemperature lock. A reading above 25 C resets the rearm counter. Staleness also interrupts rearm. On recovery or rearm, normal regulation starts from OFF and uses the current reading: 19 C remains OFF; 18 C can enable heating after qualification. At 25 C, rearm remains OFF. A restart initializes OFF and requires three new valid readings.

The driver checks one DS18B20 address, external power, resolution, conversion completion, bus presence, scratchpad CRC, finite values and the **sensor's** -55 to 125 C range. It does not apply the collar's body-temperature range. Numeric error sentinels are not accepted as usable temperatures. A completed, CRC-valid reading of 85 C is accepted as temperature and enters the overtemperature lock.

Conversions do not block `loop()`. Before a result is available, `poll()` returns no new reading; cached temperatures cannot increment qualification. Errors trigger rediscovery on a later scheduled attempt. The adapter avoids DallasTemperature 4.0.6 `begin()` (which retries with `delay()`) and uses direct OneWire discovery. Scratchpad autosave is disabled to avoid the driver's EEPROM-write wait. Bus transactions themselves are synchronous and short; the 750 ms conversion wait uses elapsed time.

This software lock and shutdown behavior are simulation features, not an independent physical safety mechanism. The report does not define sensor-failure handling; this version implements the behavior explicitly authorized for the demonstration.
