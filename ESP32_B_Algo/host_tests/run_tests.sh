#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SHARED_DIR="$PROJECT_DIR/../ESP32_A_Algo/main"
TEST_BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/kuglass-esp32b-tests.XXXXXX")
trap 'rm -rf -- "$TEST_BUILD_DIR"' EXIT HUP INT TERM

CXX=${CXX:-c++}
CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -Werror -I$PROJECT_DIR/main -I$SHARED_DIR"

$CXX $CXXFLAGS \
    "$SHARED_DIR/protocol.cpp" \
    "$PROJECT_DIR/main/channel_manager.cpp" \
    "$PROJECT_DIR/main/fault_manager.cpp" \
    "$PROJECT_DIR/main/status_reporter.cpp" \
    "$PROJECT_DIR/host_tests/test_b_core.cpp" \
    -o "$TEST_BUILD_DIR/test_b_core"

$CXX $CXXFLAGS \
    "$SHARED_DIR/protocol.cpp" \
    "$PROJECT_DIR/main/app_main.cpp" \
    "$PROJECT_DIR/main/channel_manager.cpp" \
    "$PROJECT_DIR/main/fault_manager.cpp" \
    "$PROJECT_DIR/main/spwm_generator.cpp" \
    "$PROJECT_DIR/main/status_reporter.cpp" \
    "$PROJECT_DIR/host_tests/test_b_app_link.cpp" \
    -o "$TEST_BUILD_DIR/test_b_app_link"

"$TEST_BUILD_DIR/test_b_core"
"$TEST_BUILD_DIR/test_b_app_link"
