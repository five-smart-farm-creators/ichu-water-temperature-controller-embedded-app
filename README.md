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

## Toolchain and build

Verified versions: Arduino CLI **1.6.0**, ESP32 Arduino core **3.3.4**, OneWire **2.3.8**, DallasTemperature **4.0.6**, native GCC **15.2.0** and Wokwi CLI **0.29.1**. Only the two sensor libraries are required by this application; their versions are pinned in `libraries.txt`.

On this workspace, reuse the existing task tool paths in each PowerShell process:

```powershell
$ErrorActionPreference = 'Stop'
. 'C:\Users\HPSUPP~1\AppData\Local\Temp\ichu-water-toolchain-8ee02f24e70940569d44eeab4565fe60\tool-paths.ps1'
& $ichuArduino --config-file $ichuArduinoConfig core list
& $ichuArduino --config-file $ichuArduinoConfig lib list
```

The paths file points to an isolated temporary Arduino configuration. The installed packages were copied to a shorter temporary path after GCC failed to find an existing `bits/c++config.h` through its expanded Windows search path. No tool was downloaded again and no permanent PATH change was made. If temporary files are removed, prepare an equivalent isolated toolchain before building.

From this repository root, compile a fresh temporary `sketch` directory. Only root implementation files are copied; tests with `main()` are excluded:

```powershell
$repo = (Get-Location).Path
$compileRoot = Join-Path ([IO.Path]::GetTempPath()) ('ichu-water-build-' + [Guid]::NewGuid().ToString('N'))
$sketchDir = Join-Path $compileRoot 'sketch'
New-Item -ItemType Directory -Path $sketchDir | Out-Null
Get-ChildItem -LiteralPath $repo -File |
    Where-Object { $_.Extension -in @('.ino', '.h', '.cpp') } |
    Copy-Item -Destination $sketchDir
& $ichuArduino --config-file $ichuArduinoConfig compile `
    --fqbn esp32:esp32:esp32 --warnings all `
    --output-dir (Join-Path $repo 'build') $sketchDir
if ($LASTEXITCODE -ne 0) { throw 'ESP32 compilation failed' }
Get-ChildItem -LiteralPath (Join-Path $repo 'build') -File
```

`wokwi.toml` selects `build/sketch.ino.bin` and `build/sketch.ino.elf`. No filesystem partition is used. The collar's `sketch.ino.flash.bin` is a separate workflow-generated image including its LittleFS partition table; that transformation is not copied into this application.

## Native controller tests

From the repository root in PowerShell, using the CLion compiler only for the native test:

```powershell
$repo = (Get-Location).Path
$compilerBin = 'C:\Users\HP SUPPORT\AppData\Local\Programs\CLion\bin\mingw\bin'
$testDir = Join-Path ([IO.Path]::GetTempPath()) ('ichu-water-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
$testExe = Join-Path $testDir 'temperature_control_test.exe'
$savedTaskPath = $env:PATH
try {
    $env:PATH = $compilerBin + ';' + $savedTaskPath
    & (Join-Path $compilerBin 'g++.exe') -std=c++17 -Wall -Wextra -Werror -pedantic `
        -I $repo tests/temperature_control_test.cpp TemperatureRegulator.cpp WaterController.cpp -o $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Native controller tests failed' }
} finally { $env:PATH = $savedTaskPath }
```

The tests exercise startup, inclusive hysteresis, both recovery outcomes, interrupted qualification, driver/CRC/conversion/configuration errors, NaN/infinities, sensor-range endpoints, stale data, `millis()` wraparound, 30/25 C locking and rearm, valid 85 C, and restart through simulated sensor and actuator interfaces. They test this controller, not the panel or collar firmware. They do not verify electrical behavior or the actual DS18B20 bus implementation.

## Open and operate Wokwi

1. Compile the firmware, then open this repository in VS Code with Wokwi installed and licensed.
2. Run **Wokwi: Start Simulator**. Open the serial monitor at 115200 baud.
3. Observe three qualifying readings at the initial 22 C, all with heating OFF.
4. Change the sensor slider to 18, 19, 20, 19 and 18 C. Check serial state, GPIO26, COM-NO and the red LED together.
5. Try 30 C, then 26 C, then three readings at 25 C. Try 85 C as a valid overtemperature input.
6. Stop and restart at 19 C: output starts OFF and three new readings are required. Changing the initial value in `diagram.json` is another way to test cold boot at a selected temperature.

The temperature slider is manual; turning on the LED does not heat the simulated sensor. The built-in DS18B20 documentation does not expose a disconnect/CRC-error control. Those fault paths, stale behavior and interrupted recovery are covered with native interface doubles; their live sensor-bus coverage remains outstanding. No undocumented simulator controls are used.

For automated simulation, configure `WOKWI_CLI_TOKEN` from your Wokwi account in the process environment. Never store it in this repository or print it. From the repository root, after loading the tool paths:

```powershell
if ([string]::IsNullOrWhiteSpace($env:WOKWI_CLI_TOKEN)) { throw 'WOKWI_CLI_TOKEN is required' }
$scenarios = @('tests/startup.test.yaml', 'tests/startup-heating.test.yaml',
               'tests/hysteresis-relay.test.yaml', 'tests/overtemperature.test.yaml')
foreach ($scenario in $scenarios) {
    $name = [IO.Path]::GetFileNameWithoutExtension($scenario)
    & $ichuWokwi . --scenario $scenario --timeout 120000 --timeout-exit-code 1 `
        --serial-log-file (Join-Path 'build' ($name + '.serial.log'))
    if ($LASTEXITCODE -ne 0) { throw "Wokwi failed: $scenario" }
}
```

`startup.test.yaml` checks qualification at the default 22 C. `startup-heating.test.yaml` changes the sensor to 18 C at the beginning of the scenario and checks that the first two readings keep both the output and load OFF, and the third enables them. The other scenarios cover inclusive hysteresis, the switched load, overtemperature and interrupted rearm. Each invocation starts a new simulation. A timeout is explicitly a failure, and each serial log is saved under the ignored `build/` directory.

## Reference review

The sibling repositories were inspected without modification. These findings describe the local copies that were available during this task:

| Source | Finding and consequence |
| --- | --- |
| Collar and alert-panel embedded apps | Confirmed root Arduino/C++ sources, `constexpr` configuration, Arduino CLI/core 3.3.4, Wokwi files and YAML scenarios. Their device-specific GPIO, sensing rules and network tasks are not water-controller requirements. |
| Edge `monitoring/interfaces/services.py` | Confirmed collar-oriented `POST /api/v1/monitoring/telemetry-readings`, `extra="forbid"`, and authentication using `X-API-Key`. Its request fields do not include water temperature or heater state. No compatible water-controller ingestion or forwarding flow was found in the inspected Edge source. |
| Collar `sketch.ino` / `CollarConfig.h`, panel `EdgeAlertClient.cpp` / `PanelConfig.h`, and Edge HTTP interface | The collar constructs a POST client for `/api/v1/monitoring/telemetry-readings`; the panel reads `/api/v1/monitoring/alerts`. Both configure `Authorization: ApiKey ...`, whereas the inspected Edge handlers require `X-API-Key`. The code does not substantiate the panel comment that both forms are accepted. This version does not inherit that mismatch or claim a successful integration. |
| Backend `OperationsMonitoring/Interfaces/Rest/WaterControllersController.cs` and `Resources/WaterControllerReadingResource.cs` | A water-reading contract exists separately from the Edge collar endpoint: `POST /api/v1/water-controllers/{controllerId}/readings`, with gateway authentication. It does not establish a device-to-Edge route. Cloud communication remains deferred. |
| Web `src/app/features/iot-assets/domain/model/device.model.ts` | `WaterController` exists as a device type, but the managed inventory is restricted to `SmartCollar`. A type declaration is not evidence of a working water-controller integration. |
| Mobile `lib/features/operations_monitoring/` | Confirmed waterer status, temperature/validity, configured range, heater state, communication status and reading history. Its API client queries water-trough status and water-controller readings, and its mapping expects `EdgeGateway` origin. That read-side expectation does not prove an implemented Edge ingestion flow. |
| Local report DSL `report/assets/diagram-sources/structurizr/workspace-ichu-c4.dsl` | Confirmed water-controller components, local regulation, HAL/drivers, local buffer and a cloud adapter; the cloud relation targets the backend with HTTPS/JSON. The statement that this local DSL omits the controller is contradicted by its content. The exported diagrams remain conceptual hardware references. |
| Report annex J | The requested annex path was absent in the local copy, and the supplied remote URL could not be fetched during this review. Its contents were not independently verified from those sources; the user explicitly confirmed the prototype's scope limitation. |

The backend reading resource requires stable `readingId`, decimal `temperature`, `temperatureUnit: Celsius`, `isValid`, `heaterState: Off/On` and `capturedAt` with an explicit offset and microsecond precision. Its controller description requires `X-Gateway-Key`, rejects nonempty `events`, returns 200 for an identical retry and 409 for different content with the same controller/reading identity. This is a confirmed future contract, not a communication path implemented by this firmware.

The actual device-to-cloud transport, support for actuator events, persistent buffer policy and physical hardware specifications remain outside this local demonstration. None is inferred from the mobile UI, the conceptual diagrams or the collar telemetry contract.

## Continuous integration

`.github/workflows/firmware-checks.yml` follows the reference projects' Arduino CLI build approach. It runs the native tests, installs Arduino CLI 1.6.0, core 3.3.4 and the versions in `libraries.txt`, and builds the root sources in a temporary `sketch` folder. It does not add PlatformIO or run a backend.

The four Wokwi scenarios run only when the repository secret `WOKWI_CLI_TOKEN` is available. Otherwise the job records that simulation was not executed. The runner uses Wokwi CLI 0.29.1; its Linux static x64 SHA256 was obtained from the [official release metadata](https://api.github.com/repos/wokwi/wokwi-cli/releases/tags/v0.29.1). This differs from the Windows executable's hash. The workflow has been prepared here; it has not been executed on GitHub as part of local verification.

## Verification results

Local verification on 2026-11-25 distinguishes these separate results:

| Check | Result |
| --- | --- |
| Tool execution | Arduino CLI 1.6.0, GCC 15.2.0 and Wokwi CLI 0.29.1 execute successfully. |
| Installed ESP32 core and libraries | Core 3.3.4, OneWire 2.3.8 and DallasTemperature 4.0.6 confirmed in the isolated task configuration. |
| Native controller tests | Compilation and execution returned 0; **337 controller checks, 0 failures**. These are the controller's tests, not the panel's tests. |
| ESP32 firmware | `arduino-cli --config-file <isolated-config> compile --fqbn esp32:esp32:esp32 --warnings all --output-dir build <temporary-sketch>` returned **0**. Program storage: 288799 bytes; global RAM: 20912 bytes. |
| Compiler warnings | Four warnings originate in OneWire 2.3.8 and its ESP32 GPIO macros. No warnings originated in this project's sources. The installed dependency was not patched. |
| Wokwi diagram lint | `wokwi-cli lint --warnings-as-errors .` returned **0**, with two informational `unsupported-part` messages for the approved DS18B20 and ESP32 DevKitC V4. This is limited static validation, not a simulated circuit test. |
| Declared circuit | **13 static checks passed:** component types/attributes, initial temperature, resistor values, layout and all 14 declared connections match the approved simulation. This does not verify electrical behavior. |
| Command examples and CI scripts | All four PowerShell blocks in this README parsed without syntax errors; all six workflow Bash blocks passed `bash -n` with exit code 0. The GitHub workflow and Wokwi scenarios were not executed by these syntax checks. |
| Wokwi scenarios | **Not executed:** `WOKWI_CLI_TOKEN` was absent from the process, user and machine environments. |
| Relay contacts and LED | **Not verified in simulation.** The active-HIGH requirement and `npn` configuration still require the load-side assertions and visual check. |
| Sensor faults | Native fake sensor/output tests passed. A running DS18B20 disconnect/CRC-injection test remains unavailable through the documented temperature-only automation control. |
| Physical hardware | Not tested or validated. |

The compile generated `sketch.ino.bin`, `.elf`, `.bootloader.bin`, `.partitions.bin`, `.merged.bin` and `.map`. The simulation selects the generated application `.bin` and `.elf`; no `flash.bin` is invented. Current source hashes match the temporary sketch that produced those artifacts. Local compile/lint/native logs and command/result metadata are saved in `build/verification/`, which is ignored. Compilation and lint do not establish that Wokwi ran successfully.
