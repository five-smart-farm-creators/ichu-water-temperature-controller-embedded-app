#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
arduino_cli="${ARDUINO_CLI:-arduino-cli}"
config_file="${ARDUINO_CLI_CONFIG_FILE:-}"

usage() {
    printf '%s\n' 'Usage: bash build.sh [--arduino-cli <executable>] [--config-file <arduino-cli.yaml>]'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arduino-cli|--config-file)
            if [[ $# -lt 2 || -z "$2" ]]; then
                usage >&2
                exit 2
            fi
            if [[ "$1" == --arduino-cli ]]; then arduino_cli="$2"; else config_file="$2"; fi
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

# GUI-launched VS Code may not inherit Homebrew's PATH on macOS.
if [[ "$arduino_cli" == arduino-cli ]] && ! command -v "$arduino_cli" >/dev/null 2>&1; then
    for candidate in /opt/homebrew/bin/arduino-cli /usr/local/bin/arduino-cli; do
        if [[ -x "$candidate" ]]; then arduino_cli="$candidate"; break; fi
    done
fi
if ! command -v "$arduino_cli" >/dev/null 2>&1; then
    printf '%s\n' 'Arduino CLI was not found. Install it or pass --arduino-cli <executable>.' \
        'On macOS with Homebrew: brew install arduino-cli' \
        'See README.md for the ESP32 core and library setup.' >&2
    exit 127
fi

compile_args=(compile --fqbn esp32:esp32:esp32 --warnings all)
if [[ -n "$config_file" ]]; then
    if [[ "$config_file" != /* ]]; then config_file="$repo_root/$config_file"; fi
    if [[ ! -f "$config_file" ]]; then
        printf 'Arduino configuration was not found: %s\n' "$config_file" >&2
        exit 1
    fi
    compile_args=(--config-file "$config_file" "${compile_args[@]}")
fi

# Resolve and check the temporary parent before removing this invocation's files.
temp_root="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
compile_root="$(mktemp -d "$temp_root/ichu-water-build.XXXXXX")"
cleanup() {
    if [[ -d "$compile_root" && "${compile_root%/*}" == "$temp_root" ]]; then
        rm -rf -- "$compile_root"
    fi
}
trap cleanup EXIT
sketch_dir="$compile_root/sketch"
artifact_dir="$compile_root/output"
mkdir -p "$sketch_dir" "$artifact_dir"

# Arduino requires sketch.ino in a sketch folder; native test sources stay outside it.
cp "$repo_root/sketch.ino" "$sketch_dir/"
"$arduino_cli" "${compile_args[@]}" --output-dir "$artifact_dir" "$sketch_dir"

# Only publish validated fresh artifacts to the paths selected by wokwi.toml.
for artifact in sketch.ino.bin sketch.ino.elf; do
    if [[ ! -s "$artifact_dir/$artifact" ]]; then
        printf 'The compiler did not generate a non-empty %s.\n' "$artifact" >&2
        exit 1
    fi
done
mkdir -p "$repo_root/build"
for artifact in "$artifact_dir"/*; do
    if [[ -f "$artifact" ]]; then cp "$artifact" "$repo_root/build/"; fi
done
printf 'Wokwi firmware ready: %s/build/sketch.ino.bin\n' "$repo_root"
printf 'Wokwi ELF ready: %s/build/sketch.ino.elf\n' "$repo_root"
