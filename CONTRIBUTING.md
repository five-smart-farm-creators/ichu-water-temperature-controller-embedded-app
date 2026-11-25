# Contributing

This guide covers how to set up, change, review and release the ICHU Water Temperature Controller Embedded Application.

## Prerequisites

- [arduino-cli](https://arduino.github.io/arduino-cli/) 1.1 or later
- ESP32 Arduino core 3.3.4 (`esp32:esp32`)
- Git
- The libraries listed in `libraries.txt`

Add the temperature sensor's library to `libraries.txt` after its model is selected.

## Local setup

```sh
arduino-cli core install esp32:esp32@3.3.4
git config core.hooksPath .githooks
```

`arduino-cli` expects the main `.ino` file to match its sketch folder name. Compile the sketch with:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all --output-dir build path/to/sketch
```

Replace `path/to/sketch` with the directory containing the main `.ino` file.

## Project structure and code quality

- Keep separate responsibilities for temperature sensing, temperature regulation, heating control, connectivity and local buffering.
- Place hardware access in sensor and actuator drivers. Keep temperature regulation rules independent of hardware details where practical.
- `sketch.ino` should initialize the hardware and assemble the application components.
- Store thresholds, sampling intervals and other settings in `WaterControllerConfig.h` as `constexpr` values.
- Keep periodic work non-blocking. Use `millis()` or scheduled tasks instead of `delay()` for temperature sampling and control.
- Keep the heating actuator off during startup and whenever the temperature reading is invalid or the sensor fails. Define and document the safe behavior for over-temperature conditions.
- If cloud reporting or offline buffering is implemented, keep it separate from local temperature control so a network outage does not stop the controller from regulating the water.
- Keep credentials and private configuration out of the repository.
- Write short comments in English that explain why a decision is made, rather than restating what the code does.
- Every change must compile for the target ESP32 board without warnings from project files.

## Git Flow

Use `main` for release history and `develop` for integration. Do not commit directly to either branch.

| Branch | Start from | Pull request target |
| --- | --- | --- |
| `feature/<short-description>` | `develop` | `develop` |
| `bugfix/<short-description>` | `develop` | `develop` |
| `release/vX.Y.Z` | `develop` | `main`, then merge back into `develop` |
| `hotfix/vX.Y.Z` | `main` | `main`, then merge back into `develop` |

Use lowercase kebab-case branch descriptions. Keep branches focused and delete topic branches after they are merged.

Never open a pull request from `develop` to `main`; releases go through `release/vX.Y.Z`. Do not use other branch prefixes, such as `fix/`, `docs/`, `refactor/`, `chore/` or `audit/`.

```sh
git switch develop
git pull --ff-only origin develop
git switch -c feature/short-description
```

## Commit messages

Write commit messages in English using Conventional Commits:

```text
<type>(<scope>): <imperative summary>
```

Keep each commit focused and the summary concise, without a trailing period. Common types include `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore` and `revert`.

Examples:

```text
feat(temperature): read water temperature from the sensor
feat(heating): control the heater through the relay
fix(safety): keep heating off after a sensor failure
docs: clarify local setup
```

Commit messages must also follow these rules:

- Start the summary with a lowercase letter and keep the header at most 72 characters.
- Use only these scopes: `temperature`, `heating`, `hardware`, `safety`, `connectivity`, `simulation` and `config`.
- Use `feat` for new functionality, `fix` for bug fixes, `docs` for documentation or diagram changes, and `chore` for maintenance.
- Do not use the default message from the GitHub web editor, such as `Update file.md`.

Invalid examples:

```text
(Docs): Add wiring instructions
Update README.md
feat: add water temperature control.
faet: add relay
```

## Changelog

Record each logical code, configuration, hardware, architecture or documentation change in `CHANGELOG.md` under `[Unreleased]`. Describe the effect of the change; do not list commit hashes.

## Pull requests

Open feature and bugfix pull requests against `develop`. Include the purpose, a concise summary and the relevant checks. For hardware or control changes, describe the sensor and actuator behavior and include an updated wiring diagram or simulation evidence when applicable.

For changes to temperature regulation or heating, include evidence for normal readings, invalid readings, sensor disconnection and the actuator's safe state during startup or failure. Link related issues when applicable, address review feedback and wait for required reviews and checks before merging.

## Commit granularity

A commit should be one complete logical change that can be described in a sentence and leaves the project working.

| Too fine | Balanced | Too coarse |
| --- | --- | --- |
| One commit per file or folder | `feat(temperature): add sensor reading and validation` | `feat: implement the whole controller` |
| Several consecutive typo fixes | `docs(hardware): document the temperature sensor wiring` | `docs: update everything` |

- Split code changes by responsibility, such as sensor driver, regulation logic, actuator control, connectivity or tests.
- Make one commit per complete documentation artifact or section.
- Group typo fixes with the section they belong to.
- Before pushing a branch that only you use, combine work-in-progress commits into logical commits. Never rewrite commits that are already published.

## Enforcement

- If the repository includes `.githooks/commit-msg`, enable it after cloning with `git config core.hooksPath .githooks`.
- Configure GitHub Actions to validate branch names, pull request targets, commit messages and ESP32 compilation.
- Configure Git with your name and the email linked to your GitHub account using `git config user.name` and `git config user.email`.