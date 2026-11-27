# Changelog

## [Unreleased]

### Changed

- Consolidate the application into `sketch.ino` and replace seven root headers and four implementation files with inline sections. Use pinned Modest IoT Nano Framework 1.3.0 sensor events, an event-handler mediator and actuator commands in the existing cooperative loop, preserving the control rules, DS18B20 timing, GPIO behavior and serial format. Update local builds, native test imports and CI for the single sketch.

### Added

- Local ESP32 Wokwi demonstration with asynchronous 12-bit DS18B20 sampling, an active-HIGH relay command and a red indicator supplied through COM-NO.
- Configurable 18/20 C hysteresis, three-reading startup and recovery, invalid/stale reading shutdown and a 30/25 C software overtemperature lock with qualified rearm.
- Hardware-independent regulation, injected sensor/output interfaces, native controller checks and Wokwi startup, hysteresis/contact and overtemperature scenarios.
- Pinned sensor dependencies, simulation wiring and reproducible build/test instructions. Network integration and persistent buffering are explicitly deferred.
- A Wokwi scenario that requests heating at startup and checks the switched load remains off until the third valid reading.
- A firmware-check workflow with pinned Arduino/core/library versions, native tests and token-gated Wokwi scenarios using a verified CLI release hash.
- Reference-code findings and separate records for native tests, ESP32 compilation, limited diagram lint and simulation coverage awaiting a Wokwi token.
- Sensor-driver regression coverage using the real adapter and pinned DallasTemperature implementation with a timed OneWire test bus, executed separately from the existing 337 controller checks and Wokwi scenarios.
- A Windows PowerShell build script and default VS Code build task that validate fresh ESP32 artifacts before copying them to the paths selected by Wokwi, with optional machine-specific tool paths kept under ignored `build/`.
- Relay input, supply and switched-load assertions for startup, hysteresis and overtemperature scenarios; all four scenarios pass in the current local Wokwi verification.

### Fixed

- Select Bash on macOS/Linux and PowerShell only on Windows for the default VS Code build task. Add a Bash firmware build script, preserve LF line endings and document per-machine Arduino CLI setup so a fresh checkout can generate Wokwi's firmware locally.
- Prevent a truncated millisecond timestamp from rejecting a 12-bit conversion before 750 ms have actually elapsed. Use a microsecond deadline while preserving the 1000 ms request schedule, millisecond stale guard, completion bit, presence, CRC, resolution and range validation.
- Give the simulated LED anode a defined OFF reference through a 10 kohm pull-down. Check the load rather than assuming an open relay NO contact reads LOW, and wait 100 simulated ms for switching to settle before checking the affected load transitions.
- Separate the earlier token-blocked sensor investigation from the later successful simulation checks. Fresh ESP32 compilation, 337 controller checks, 48 sensor-driver checks and all four Wokwi scenarios pass; the original reported sensor failure's cause is not established by those results.
