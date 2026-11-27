# ICHU Water Temperature Controller Embedded Application

Local ESP32 firmware for a **Wokwi demonstration** of water-temperature regulation. It reads one simulated DS18B20, controls a relay and reports measurements, qualification, faults and overtemperature state at 115200 baud. A red LED powered through the relay contacts indicates the requested heating state. It does not model heat transfer.

All components, connections and numeric settings below were explicitly selected for this simulation. They are not physical specifications established by the ICHU report, livestock drinking-water recommendations or validated installation parameters. The report's conceptual circuit does not verify the physical sensor or heater model. Its resistor marked 200 ohms is not used as a sensor specification here: this simulation uses the explicitly approved **4.7 kohm pull-up**.

No Wi-Fi, Internet, HTTP, MQTT, credentials, Edge or backend connection is initialized. Water level, filling, pumps and valves are outside this application. Persistent buffering and cloud integration are deferred; there are no placeholder implementations or per-reading flash writes. Restarting discards the RAM state.

## Structure

The application firmware lives entirely in the root `sketch.ino`:

| File | Responsibility |
| --- | --- |
| `sketch.ino` | Configuration, reading validation, regulation, Modest sensor events and actuator commands, asynchronous DS18B20 driver, relay, non-blocking loop and serial observations |
| `diagram.json`, `wokwi.toml`, `libraries.txt` | Circuit, generated firmware paths and pinned dependencies |
| `tests/` | Native controller tests and Wokwi automation scenarios |

This firmware uses [Modest IoT Nano Framework 1.3.0](https://github.com/modest-iot/modest-iot-nano-framework/releases/tag/v1.3.0). `TemperatureSource` extends its `Sensor`: measurement requests emit data-read events only when a new result is available, and `WaterController` receives them as an `EventHandler`. `HeatingOutput` extends `Actuator`, which dispatches the heater commands. These events run synchronously in the existing non-blocking loop, preserving the sensor cadence, shutdown order and serial messages without starting the framework's `Device` sampler/network engine. The application has no separate root `.h` or `.cpp` files. Combining source files is an organization change; header count alone does not determine Wokwi runtime performance.

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
| LED anode reference | Additional 10 kohm resistor from A to common GND, for this simulation |
| Relay NC | Unconnected |

The load label is **Heater indicator — simulation**. It is supplied only through the relay contacts; no second GPIO drives it. Sensor, ESP32 and relay/load are arranged from left to right. The initial simulated temperature is 22 C. No mains load is represented.

### Relay polarity and load reference

Measurements with Wokwi CLI 0.29.1 and Simulation API `1.0.0-20261117-ga10e9732` confirmed that `transistor="npn"` gives the required behavior: HIGH connects COM-NO; LOW connects COM-NC. `pnp` does the opposite. The attribute table in the [official relay reference](https://docs.wokwi.com/parts/wokwi-relay-module) agrees with those measurements; its Operation paragraphs describe the opposite polarity. This circuit uses `npn`; firmware HIGH/LOW logic remains unchanged.

An open contact does not imply digital LOW. In an isolated diagnostic, GPIO32 sensed NO, GPIO33 sensed NC and GPIO25 sensed the LED anode. Switching their weak internal pull-up/pull-down references made an open contact and the disconnected anode change between 1 and 0; a contact connected to COM stayed at 1 with either bias. With `npn` and LOW, NO and the anode followed the bias while NC stayed at 1. With HIGH, NO and the anode stayed at 1 while NC followed the bias. `pnp` reversed the connected/open states. This demonstrates the floating nodes rather than inferring them from a single `expect-pin` result. The diagnostic firmware and extra probe wires are confined to ignored `build/relay-diagnosis/probe/`.

The final circuit adds `loadReference`, a **10 kohm pull-down directly at the LED anode**, leaving the 330 ohm series resistor and 4.7 kohm sensor pull-up intact. It gives the switched load a defined OFF reference. Its ideal upper-bound current at 5 V is 0.5 mA; this is a selected simulation reference, not a physical specification from the report. Wokwi [documents external pull-down resistors](https://docs.wokwi.com/parts/wokwi-resistor) while noting its limited analog model. In the measured reference variant, LOW produced A=0/C=0 even while the upstream open NO still read 1; HIGH produced A=1/C=0. A resistor on the load side does not make an open NO's API reading a reliable continuity measurement.

The scenarios therefore check the referenced LED anode and grounded cathode, relay IN and the COM supply. ON additionally checks the closed NO contact; OFF never assumes a digital level on the open NO. A trial assertion on the final circuit's unconnected NC returned `Pin not found: NC`; NC continuity was measured through the temporary GPIO probe, and no unavailable NC API assertion is counted as a pass. GPIO26 and serial messages alone cannot pass the load checks. These assertions verify the virtual load's electrical drive, not optical brightness, analog LED current or physical hardware.

## Demonstration control rules

Change the `constexpr` settings in the `WaterControllerConfig` namespace inside `sketch.ino`, then rebuild the firmware. These values are the authorized demonstration profile:

| Setting | Value |
| --- | --- |
| Heating ON | Valid temperature <= 18.0 C |
| Heating OFF | Valid temperature >= 20.0 C |
| Between 18 and 20 C | Preserve the previous state |
| Startup and fault recovery | Three consecutive valid readings, with output OFF until the third |
| Conversion start interval | 1000 ms; first conversion starts as soon as initialization permits |
| DS18B20 resolution / conversion wait | 12 bits / at least 750000 us after STARTCONVO returns, checked with `micros()` |
| Stale reading | More than 3000 ms without a completed valid reading forces OFF and restarts qualification |
| Overtemperature lock | Any valid temperature >= 30.0 C forces OFF, including during qualification |
| Automatic rearm | Three consecutive valid readings <= 25.0 C |

An invalid reading resets both counters, forces OFF and preserves an existing overtemperature lock. A reading above 25 C resets the rearm counter. Staleness also interrupts rearm. On recovery or rearm, normal regulation starts from OFF and uses the current reading: 19 C remains OFF; 18 C can enable heating after qualification. At 25 C, rearm remains OFF. A restart initializes OFF and requires three new valid readings.

The driver checks one DS18B20 address, external power, resolution, conversion completion, bus presence, scratchpad CRC, finite values and the **sensor's** -55 to 125 C range. It does not apply the collar's body-temperature range. Numeric error sentinels are not accepted as usable temperatures. A completed, CRC-valid reading of 85 C is accepted as temperature and enters the overtemperature lock.

Conversions do not block `loop()`. Before a result is available, `poll()` returns no new reading; cached temperatures cannot increment qualification. Errors trigger rediscovery on a later scheduled attempt. The adapter avoids DallasTemperature 4.0.6 `begin()` (which retries with `delay()`) and uses direct OneWire discovery. Scratchpad autosave is disabled to avoid the driver's EEPROM-write wait. Bus transactions themselves are synchronous and short; the 750 ms conversion wait uses elapsed time.

The conversion deadline uses unsigned microsecond subtraction. A millisecond timestamp is truncated: a difference of 750 integer milliseconds can represent less than 750 ms of actual elapsed time. The separate millisecond clock still schedules requests and rejects unread conversions older than 3000 ms, even after a `micros()` rollover. Completion is still checked with `isConversionComplete()` before reading the scratchpad; presence, CRC, resolution and sensor-range checks remain in place.

This software lock and shutdown behavior are simulation features, not an independent physical safety mechanism. The report does not define sensor-failure handling; this version implements the behavior explicitly authorized for the demonstration.

## Toolchain and build

Versions: Arduino CLI **1.6.0**, ESP32 Arduino core **3.3.4**, Modest IoT Nano Framework **1.3.0**, OneWire **2.3.8**, DallasTemperature **4.0.6**, native GCC **15.2.0** and Wokwi CLI **0.29.1**. `libraries.txt` pins the framework, both sensor libraries and the framework's MQTT **2.6.0** dependency. Installing MQTT does not initialize a network connection.

Open this repository root in VS Code and select **Terminal > Run Build Task > build esp32 firmware**. The default shortcut is **Cmd+Shift+B** on macOS or **Ctrl+Shift+B** on Windows/Linux. The task runs `build.sh` with `/bin/bash` on macOS/Linux and `build.ps1` with `powershell.exe` on Windows; PowerShell is not required on macOS. These platform overrides follow the [VS Code task configuration](https://code.visualstudio.com/docs/debugtest/tasks#_operating-system-specific-properties).

Both scripts copy only `sketch.ino` into a temporary `sketch` folder, compile for `esp32:esp32:esp32`, check fresh non-empty `.bin` and `.elf` files, and copy the generated artifacts into this repository's `build/` directory. They resolve the repository from the script location, independently of the terminal's working directory. Git ignores `build/`: pulling the repository downloads the build scripts, so compile locally before starting Wokwi.

### macOS and Linux

Install Arduino CLI using the [official installation instructions](https://arduino.github.io/arduino-cli/1.6/installation/). On a Mac that already has Homebrew, run `brew install arduino-cli`. The shell script also checks `/opt/homebrew/bin` and `/usr/local/bin` when Arduino CLI is absent from VS Code's PATH.

Install this project's pinned core and libraries once on that machine, then compile from the repository root:

```bash
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.4 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib update-index
arduino-cli lib install OneWire@2.3.8 DallasTemperature@4.0.6 Modest-IoT-Nano-Framework@1.3.0 MQTT@2.6.0
bash ./build.sh
test -s build/sketch.ino.bin && test -s build/sketch.ino.elf && echo 'Firmware ready'
```

For an isolated installation, run `bash ./build.sh --arduino-cli <executable> --config-file <arduino-cli.yaml>`, or set `ARDUINO_CLI` and `ARDUINO_CLI_CONFIG_FILE` before launching VS Code. The config path may be absolute or relative to the repository. The shell script uses Bash 3.2-compatible syntax and does not require an executable permission on `build.sh` because the task explicitly invokes Bash.

### Windows

Run the default task or `powershell -ExecutionPolicy Bypass -File .\build.ps1`. The script uses Arduino CLI on PATH by default. For an isolated installation, pass `-ArduinoCli <executable>` and `-ConfigFile <arduino-cli.yaml>`, or save those absolute paths as the `ArduinoCli` and `ConfigFile` properties in ignored `build/arduino-local.json` so the Windows task can reuse them. This workspace's local file is configured for the existing isolated toolchain. If that temporary toolchain is removed, install Arduino CLI, the core and pinned libraries again and update or remove the local file.

After a successful Windows build, `Test-Path .\build\sketch.ino.bin` and `Test-Path .\build\sketch.ino.elf` must both return `True`. On any supported platform, start **Wokwi: Start Simulator** after building. Wokwi loads the firmware; it does not compile it, as described in the [official project configuration guide](https://docs.wokwi.com/vscode/project-config).

On this workspace, reuse the existing task tool paths in each PowerShell process:

```powershell
$ErrorActionPreference = 'Stop'
. 'C:\Users\HPSUPP~1\AppData\Local\Temp\ichu-water-toolchain-8ee02f24e70940569d44eeab4565fe60\tool-paths.ps1'
& $ichuArduino --config-file $ichuArduinoConfig core list
& $ichuArduino --config-file $ichuArduinoConfig lib list
```

The paths file points to an isolated temporary Arduino configuration. The installed packages were copied to a shorter temporary path after GCC failed to find an existing `bits/c++config.h` through its expanded Windows search path. No tool was downloaded again and no permanent PATH change was made. If temporary files are removed, prepare an equivalent isolated toolchain before building.

From this repository root, compile a fresh temporary `sketch` directory. Only `sketch.ino` is copied; tests with `main()` are excluded:

```powershell
$repo = (Get-Location).Path
$compileRoot = Join-Path ([IO.Path]::GetTempPath()) ('ichu-water-build-' + [Guid]::NewGuid().ToString('N'))
$sketchDir = Join-Path $compileRoot 'sketch'
New-Item -ItemType Directory -Path $sketchDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'sketch.ino') -Destination $sketchDir
& $ichuArduino --config-file $ichuArduinoConfig compile `
    --fqbn esp32:esp32:esp32 --warnings all `
    --output-dir (Join-Path $repo 'build') $sketchDir
if ($LASTEXITCODE -ne 0) { throw 'ESP32 compilation failed' }
Get-ChildItem -LiteralPath (Join-Path $repo 'build') -File
```

`wokwi.toml` selects `build/sketch.ino.bin` and `build/sketch.ino.elf`. No filesystem partition is used. The collar's `sketch.ino.flash.bin` is a separate workflow-generated image including its LittleFS partition table; that transformation is not copied into this application.

## Native controller and sensor driver tests

From the repository root in PowerShell, using the CLion compiler only for the native test:

```powershell
$repo = (Get-Location).Path
$compilerBin = 'C:\Users\HP SUPPORT\AppData\Local\Programs\CLion\bin\mingw\bin'
$testDir = Join-Path ([IO.Path]::GetTempPath()) ('ichu-water-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
$testExe = Join-Path $testDir 'temperature_control_test.exe'
$driverTestExe = Join-Path $testDir 'temperature_sensor_test.exe'
# Installed library directory from the isolated Arduino configuration above.
$dallasDir = 'C:\Users\HP SUPPORT\AppData\Local\Temp\iwc-5cb4a3d9\user\libraries\DallasTemperature'
$modestDir = 'C:\Users\HP SUPPORT\AppData\Local\Temp\iwc-5cb4a3d9\user\libraries\Modest-IoT-Nano-Framework\src'
$savedTaskPath = $env:PATH
try {
    $env:PATH = $compilerBin + ';' + $savedTaskPath
    & (Join-Path $compilerBin 'g++.exe') -std=c++17 -Wall -Wextra -Werror -pedantic `
        -I tests/sensor_support -I $modestDir tests/temperature_control_test.cpp `
        (Join-Path $modestDir 'Sensor.cpp') (Join-Path $modestDir 'Actuator.cpp') -o $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'Native controller tests failed' }
    & (Join-Path $compilerBin 'g++.exe') -std=c++17 -DARDUINO=10819 -Wall -Wextra -Werror -pedantic `
        -I tests/sensor_support -I $dallasDir -I $modestDir `
        tests/temperature_sensor_test.cpp (Join-Path $dallasDir 'DallasTemperature.cpp') `
        (Join-Path $modestDir 'Sensor.cpp') (Join-Path $modestDir 'Actuator.cpp') `
        -o $driverTestExe
    if ($LASTEXITCODE -ne 0) { throw 'Sensor driver test compilation failed' }
    & $driverTestExe
    if ($LASTEXITCODE -ne 0) { throw 'Sensor driver timing tests failed' }
} finally { $env:PATH = $savedTaskPath }
```

The tests exercise startup, inclusive hysteresis, both recovery outcomes, interrupted qualification, driver/CRC/conversion/configuration errors, NaN/infinities, sensor-range endpoints, stale data, `millis()` wraparound, 30/25 C locking and rearm, valid 85 C, and restart through simulated sensor and actuator interfaces. They test this controller, not the panel or collar firmware. They do not verify electrical behavior or the actual DS18B20 bus implementation.

Both suites include the actual `sketch.ino` and compile the installed Modest `Sensor.cpp` and `Actuator.cpp`. Test-only macros exclude the Arduino entry points and ESP32 relay; the controller suite also excludes the DS18B20 adapter. `temperature_control_test.cpp` retains its 337 checks using `FakeTemperature` and `FakeHeater`, now through the framework's event and command dispatch. `temperature_sensor_test.cpp` runs the real adapter through `Sensor::on`, with the installed DallasTemperature implementation and the deterministic clock/OneWire test bus in `tests/sensor_support/`. Its 48 checks cover the fractional-millisecond regression, single-use results, request cadence, non-blocking behavior, genuinely incomplete conversions, presence/CRC/configuration/range errors, clock rollover and stale conversion rejection. Wokwi scenarios separately verify the virtual circuit. CI runs both native suites before compiling the firmware.

## Open and operate Wokwi

1. Open this repository root in VS Code with Wokwi installed and licensed, then select **Terminal > Run Build Task > build esp32 firmware** to compile locally.
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
    $timeoutMs = if ($scenario -like 'tests/startup*') { 15000 } else { 30000 }
    & $ichuWokwi . --scenario $scenario --timeout $timeoutMs --timeout-exit-code 1 `
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

`.github/workflows/firmware-checks.yml` follows the reference projects' Arduino CLI build approach. It installs Arduino CLI 1.6.0, core 3.3.4 and the versions in `libraries.txt`, runs both native suites against the sketch and real framework dispatch, and builds `sketch.ino` in a temporary `sketch` folder. It does not add PlatformIO or run a backend.

The four Wokwi scenarios run only when the repository secret `WOKWI_CLI_TOKEN` is available. Otherwise the job records that simulation was not executed. The runner uses Wokwi CLI 0.29.1; its Linux static x64 SHA256 was obtained from the [official release metadata](https://api.github.com/repos/wokwi/wokwi-cli/releases/tags/v0.29.1). This differs from the Windows executable's hash. The workflow has been prepared here; it has not been executed on GitHub as part of local verification.

## Verification results

### Modest 1.3.0 single-sketch migration (2026-11-27)

The original firmware passed all 337 controller checks, 48 sensor-driver checks and four unchanged Wokwi scenarios before migration. The migrated sketch passed the same 385 native checks using the installed Modest `Sensor.cpp` and `Actuator.cpp`, and all four Wokwi scenarios again: startup, startup heating qualification, hysteresis/relay contacts, and overtemperature/rearm. Each simulation returned exit 0 using Wokwi CLI 0.29.1 and Simulation API `1.0.0-20261126-gb061761c`. No test assertions, thresholds, wiring or scenario steps were changed. The configuration, regulator implementation and sensor conversion/validation implementation were also compared directly with the original sources.

The Windows build script generated fresh non-empty firmware and ELF files from the single sketch. Program storage is **977351 bytes (74%)** and static RAM is **49784 bytes (15%)**. This is larger than the previous build because Arduino also compiles and links dependencies of the framework's unused modules; no Wi-Fi/BLE connection or framework background engine is initialized by the application. The build reported nine warnings from Modest, OneWire and ESP32 dependency sources, with none from the application. The PowerShell/Bash build scripts and eight CI shell blocks passed syntax checks; the updated GitHub workflow has not been run remotely. Build results, source/binary hashes and before/after simulation logs are under ignored `build/modest-migration/`.

### Previous build and simulation checks (2026-11-26, before framework migration)

The Windows build task completed with exit 0 and generated non-empty `build/sketch.ino.bin` and `build/sketch.ino.elf` from the root firmware sources. Program storage is 288835 bytes and global RAM is 20920 bytes. The four compiler warnings originate in OneWire and its ESP32 GPIO macros, with none in this project's sources. Both native suites passed again: 337 controller checks and 48 sensor-driver checks, with zero failures.

All four scenarios passed against Wokwi Simulation API `1.0.0-20261126-gccaad6a5`: startup, startup heating qualification, hysteresis/relay contacts, and overtemperature/rearm. The startup load check and the remaining ON/OFF transition checks now wait 100 simulated ms before reading the switched load, matching the existing settling waits. The initial GPIO26 LOW assertion remains immediate. Earlier runs without these waits failed before the contact/load levels had settled. Logs and exit codes are under ignored `build/pre-push/`; the earlier diagnostic records below describe their respective runs.

These results confirm the current virtual circuit and scenario assertions. They do not establish optical brightness, physical hardware behavior, fault injection on the live sensor bus, or the cause of the operator's original sensor failure. The GitHub workflow has not been run as part of this local verification.

### Earlier sensor timing investigation (2026-11-26)

The operator reported a running `startup.test.yaml` simulation with repeated `conversion-incomplete`, no valid temperatures, subsequent `STALE` shutdown and a connection timeout. That run reached the firmware; it is not evidence of a token failure. The other three scenarios were not reached in that run.

The installed DallasTemperature package is 4.0.6 and OneWire is 2.3.8. Its `requestTemperaturesByAddress()` timestamps `millis()` after sending STARTCONVO; `isConversionComplete()` only reads one bus bit. The [DS18B20 specification](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf) gives a maximum conversion time of 750 ms at 12 bits. A deterministic regression reproduced a real adapter defect: at a difference of 750 integer milliseconds, only **749915 us** had elapsed. The original adapter rejected that conversion; the corrected adapter waits for the full **750000 us** and still rejects a conversion whose completion bit remains low.

This establishes the premature-deadline defect in the adapter. At that stage, its causal relationship to the reported Wokwi failure, and any additional virtual-sensor limitation, were **unverified in a running simulation**. The collar's driver initializes DallasTemperature and uses a blocking broadcast request and indexed read at 10 bits; neither that sequence nor its framework-driver comment proves the cause here. The collar's unpinned `libraries.txt` does not establish which versions a historical collar build used.

| Check at that stage | Actual result |
| --- | --- |
| Baseline ESP32 build from unchanged root sources | Exit 0; source snapshot saved before editing. |
| Driver regression against the original adapter | Native compile exit 0; execution exit 1: 48 checks, 3 failures at the premature deadline. |
| Driver regression after correction | Compile and execution exit 0: 48 checks, 0 failures. |
| Original controller suite | Compile and execution exit 0: 337 checks, 0 failures. |
| Corrected ESP32 build, FQBN `esp32:esp32:esp32`, core 3.3.4 | Exit 0; 288835 bytes program storage, 20920 bytes global RAM. Four dependency warnings in OneWire; none in project sources. |
| Corrected `startup.test.yaml`, 15000 simulated ms | CLI exit 1 **before simulation started**: `Missing WOKWI_CLI_TOKEN environment variable`. This tools process did not inherit the Windows token. |
| `startup-heating.test.yaml` | Not executed: startup could not be run from this process. |
| `hysteresis-relay.test.yaml` | Not executed; contacts and load remain unverified. |
| `overtemperature.test.yaml` | Not executed. |

No scenario assertions were weakened, no sensor or regulation settings were changed, and no completion check was removed during that sensor investigation. Its logs, the before/after source and firmware hashes, and a temporary timing probe are under ignored `build/sensor-diagnosis/`. The corrected binaries selected by `wokwi.toml` were rebuilt from the root sources; the probe was prepared but not executed. No Git commands were run during that investigation.

From a PowerShell process that already has the token, run the four-scenario loop above. For this workspace's saved diagnostic probe, `& .\build\sensor-diagnosis\verify-wokwi.ps1 -IncludeTimingProbe` first measures the original deadline and the same conversion at 752000 us, then rebuilds the current root sources in a `finally` block and runs startup with a 15000 ms limit. It runs the other three scenarios only after startup passes, records individual exit codes and keeps all logs under `build/sensor-diagnosis/`. The extra probe wait is instrumentation, not the production conversion deadline. Never pass the token as a literal argument or save it to a file.

### Historical local verification (2026-11-25)

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
