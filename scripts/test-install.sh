#!/usr/bin/env bats
# test-install.sh
# Unit tests for install-native.sh using bats-core
#
# Tests the sourceable functions from install-native.sh without executing
# any system-modifying operations (no apt-get, no usermod, no modprobe).
#
# Prerequisites:
#   - bats-core: https://bats-core.readthedocs.io/
#   - Install: sudo apt-get install bats  (or: npm install -g bats)
#
# Usage:
#   bats scripts/test-install.sh

# Path to the script under test (relative to repo root)
SCRIPT_DIR="$(cd "$(dirname "$BATS_TEST_FILENAME")" && pwd)"
INSTALL_SCRIPT="${SCRIPT_DIR}/install-native.sh"

setup() {
    # Create temp directory for test artifacts
    TEST_TEMP_DIR="$(mktemp -d)"

    # Override LOG_FILE to use temp directory
    export LOG_FILE="${TEST_TEMP_DIR}/test-install.log"

    # Source the install script (guard clause prevents main() from running)
    source "$INSTALL_SCRIPT"
}

teardown() {
    # Clean up temp directory
    rm -rf "$TEST_TEMP_DIR"
}

# --- detect_platform tests ---

@test "detect_platform: detects Jetson when /etc/nv_tegra_release exists" {
    # Mock the Jetson detection file
    mkdir -p "${TEST_TEMP_DIR}/etc"
    touch "${TEST_TEMP_DIR}/etc/nv_tegra_release"

    # Override the function to use our mock path
    detect_platform() {
        if [ -f "${TEST_TEMP_DIR}/etc/nv_tegra_release" ]; then
            PLATFORM="jetson"
            ARCH=$(uname -m)
        elif grep -q "Raspberry Pi" "${TEST_TEMP_DIR}/proc/cpuinfo" 2>/dev/null; then
            PLATFORM="rpi"
            ARCH="aarch64"
        else
            PLATFORM="generic"
            ARCH=$(uname -m)
        fi
    }

    detect_platform
    [ "$PLATFORM" = "jetson" ]
}

@test "detect_platform: detects RPi when /proc/cpuinfo contains Raspberry Pi" {
    # Mock the RPi detection file
    mkdir -p "${TEST_TEMP_DIR}/proc"
    echo "Hardware : BCM2835" > "${TEST_TEMP_DIR}/proc/cpuinfo"
    echo "Model : Raspberry Pi 4 Model B Rev 1.4" >> "${TEST_TEMP_DIR}/proc/cpuinfo"

    detect_platform() {
        if [ -f "${TEST_TEMP_DIR}/etc/nv_tegra_release" ]; then
            PLATFORM="jetson"
            ARCH=$(uname -m)
        elif grep -q "Raspberry Pi" "${TEST_TEMP_DIR}/proc/cpuinfo" 2>/dev/null; then
            PLATFORM="rpi"
            ARCH="aarch64"
        else
            PLATFORM="generic"
            ARCH=$(uname -m)
        fi
    }

    detect_platform
    [ "$PLATFORM" = "rpi" ]
    [ "$ARCH" = "aarch64" ]
}

@test "detect_platform: defaults to generic when no platform markers found" {
    # No mock files — neither Jetson nor RPi
    detect_platform() {
        if [ -f "${TEST_TEMP_DIR}/etc/nv_tegra_release" ]; then
            PLATFORM="jetson"
            ARCH=$(uname -m)
        elif grep -q "Raspberry Pi" "${TEST_TEMP_DIR}/proc/cpuinfo" 2>/dev/null; then
            PLATFORM="rpi"
            ARCH="aarch64"
        else
            PLATFORM="generic"
            ARCH=$(uname -m)
        fi
    }

    detect_platform
    [ "$PLATFORM" = "generic" ]
}

# --- log tests ---

@test "log: writes message to stdout" {
    run log "test message"
    [ "$status" -eq 0 ]
    [[ "$output" == *"test message"* ]]
}

@test "log: appends message to LOG_FILE" {
    log "file test message" > /dev/null
    [ -f "$LOG_FILE" ]
    grep -q "file test message" "$LOG_FILE"
}

# --- error_exit tests ---

@test "error_exit: exits with code 1" {
    run error_exit "something went wrong"
    [ "$status" -eq 1 ]
}

@test "error_exit: prints error message" {
    run error_exit "something went wrong"
    [[ "$output" == *"something went wrong"* ]]
}

# --- check_root tests ---

@test "check_root: fails when EUID is not 0" {
    # Override EUID to simulate non-root
    EUID=1000
    run check_root
    [ "$status" -eq 1 ]
    [[ "$output" == *"sudo"* ]]
}

# --- print_header tests ---

@test "print_header: includes platform and architecture" {
    PLATFORM="jetson"
    ARCH="aarch64"
    run print_header
    [ "$status" -eq 0 ]
    [[ "$output" == *"jetson"* ]]
    [[ "$output" == *"aarch64"* ]]
}

@test "print_header: includes install directory" {
    PLATFORM="generic"
    ARCH="x86_64"
    run print_header
    [[ "$output" == *"$INSTALL_DIR"* ]]
}

# --- print_next_steps tests ---

@test "print_next_steps: shows Jetson-specific steps for Jetson platform" {
    PLATFORM="jetson"
    run print_next_steps
    [ "$status" -eq 0 ]
    [[ "$output" == *"jetson-io.py"* ]]
    [[ "$output" == *"REBOOT"* ]]
}

@test "print_next_steps: shows reboot for RPi platform" {
    PLATFORM="rpi"
    run print_next_steps
    [ "$status" -eq 0 ]
    [[ "$output" == *"REBOOT"* ]]
}

@test "print_next_steps: no reboot message for generic platform" {
    PLATFORM="generic"
    run print_next_steps
    [ "$status" -eq 0 ]
    [[ "$output" != *"REBOOT"* ]]
}

@test "print_next_steps: includes build instructions" {
    PLATFORM="generic"
    run print_next_steps
    [[ "$output" == *"cmake"* ]]
    [[ "$output" == *"make"* ]]
}

# --- step_verify tests ---

@test "step_verify: reports SDL2 when sdl2-config is available" {
    # Create a mock sdl2-config
    mkdir -p "${TEST_TEMP_DIR}/bin"
    cat > "${TEST_TEMP_DIR}/bin/sdl2-config" << 'MOCK_EOF'
#!/bin/bash
echo "2.28.2"
MOCK_EOF
    chmod +x "${TEST_TEMP_DIR}/bin/sdl2-config"

    PATH="${TEST_TEMP_DIR}/bin:$PATH" run step_verify
    [ "$status" -eq 0 ]
    [[ "$output" == *"SDL2"* ]]
    [[ "$output" == *"2.28.2"* ]]
}

@test "step_verify: warns when SDL2 not found" {
    # Ensure sdl2-config is not in PATH
    PATH="/nonexistent" run step_verify
    [[ "$output" == *"SDL2 not found"* ]]
}

# --- Configuration constants ---

@test "SDL2_VERSION is set" {
    [ -n "$SDL2_VERSION" ]
}

@test "INSTALL_DIR is set to /opt/mirage" {
    [ "$INSTALL_DIR" = "/opt/mirage" ]
}
