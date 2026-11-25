# Changelog

## [Unreleased]

### Added

- Local ESP32 Wokwi demonstration with asynchronous 12-bit DS18B20 sampling, an active-HIGH relay command and a red indicator supplied through COM-NO.
- Configurable 18/20 C hysteresis, three-reading startup and recovery, invalid/stale reading shutdown and a 30/25 C software overtemperature lock with qualified rearm.
- Hardware-independent regulation, injected sensor/output interfaces, native controller checks and Wokwi startup, hysteresis/contact and overtemperature scenarios.
- Pinned sensor dependencies, simulation wiring and reproducible build/test instructions. Network integration and persistent buffering are explicitly deferred.
