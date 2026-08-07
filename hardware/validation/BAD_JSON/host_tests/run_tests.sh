#!/bin/sh
set -eu

INCIDENT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$INCIDENT_DIR/../../.." && pwd)
TEST_BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/kuglass-ab-uart-tests.XXXXXX")
trap 'rm -rf -- "$TEST_BUILD_DIR"' EXIT HUP INT TERM

CXX=${CXX:-c++}
CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -Werror"

$CXX $CXXFLAGS \
    -I"$PROJECT_ROOT/ESP32_A_Algo/main" \
    -I"$PROJECT_ROOT/ESP32_B_Algo/main" \
    "$PROJECT_ROOT/ESP32_A_Algo/main/downstream_status.cpp" \
    "$PROJECT_ROOT/ESP32_B_Algo/main/channel_manager.cpp" \
    "$PROJECT_ROOT/ESP32_B_Algo/main/fault_manager.cpp" \
    "$PROJECT_ROOT/ESP32_B_Algo/main/status_reporter.cpp" \
    "$INCIDENT_DIR/host_tests/b_status_fixture.cpp" \
    "$INCIDENT_DIR/host_tests/test_ab_uart_stream.cpp" \
    -o "$TEST_BUILD_DIR/test_ab_uart_stream"

"$TEST_BUILD_DIR/test_ab_uart_stream"
