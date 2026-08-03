#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/kuglass-esp32a-tests.XXXXXX")
trap 'rm -rf -- "$TEST_BUILD_DIR"' EXIT HUP INT TERM

CXX=${CXX:-c++}
CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -Werror -I$PROJECT_DIR/main"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/protocol.cpp" \
    "$PROJECT_DIR/host_tests/test_protocol_parser.cpp" \
    -o "$TEST_BUILD_DIR/test_protocol"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/ui_protocol.cpp" \
    "$PROJECT_DIR/host_tests/test_ui_protocol.cpp" \
    -o "$TEST_BUILD_DIR/test_ui_protocol"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/protocol.cpp" \
    "$PROJECT_DIR/main/ui_protocol.cpp" \
    "$PROJECT_DIR/main/policy_engine.cpp" \
    "$PROJECT_DIR/host_tests/test_policy_engine.cpp" \
    -o "$TEST_BUILD_DIR/test_policy"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/camera_metric_adapter.cpp" \
    "$PROJECT_DIR/main/ds18b20_sensor.cpp" \
    "$PROJECT_DIR/host_tests/test_sensor_adapters.cpp" \
    -o "$TEST_BUILD_DIR/test_sensors"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/downstream_status.cpp" \
    "$PROJECT_DIR/main/esp32_b_link.cpp" \
    "$PROJECT_DIR/main/protocol.cpp" \
    "$PROJECT_DIR/main/ui_protocol.cpp" \
    "$PROJECT_DIR/main/policy_engine.cpp" \
    "$PROJECT_DIR/host_tests/test_downstream_status.cpp" \
    -o "$TEST_BUILD_DIR/test_downstream_status"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/host_tests/test_json_line_accumulator.cpp" \
    -o "$TEST_BUILD_DIR/test_json_line_accumulator"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/master_telemetry.cpp" \
    "$PROJECT_DIR/main/policy_engine.cpp" \
    "$PROJECT_DIR/main/ui_protocol.cpp" \
    "$PROJECT_DIR/host_tests/test_master_telemetry.cpp" \
    -o "$TEST_BUILD_DIR/test_telemetry"

$CXX $CXXFLAGS \
    "$PROJECT_DIR/main/app_main.cpp" \
    "$PROJECT_DIR/main/protocol.cpp" \
    "$PROJECT_DIR/main/ui_protocol.cpp" \
    "$PROJECT_DIR/main/policy_engine.cpp" \
    "$PROJECT_DIR/main/camera_metric_adapter.cpp" \
    "$PROJECT_DIR/main/downstream_status.cpp" \
    "$PROJECT_DIR/main/ds18b20_sensor.cpp" \
    "$PROJECT_DIR/main/esp32_b_link.cpp" \
    "$PROJECT_DIR/main/server_console.cpp" \
    "$PROJECT_DIR/main/master_telemetry.cpp" \
    "$PROJECT_DIR/host_tests/test_master_app_link.cpp" \
    -o "$TEST_BUILD_DIR/test_master_app_link"

"$TEST_BUILD_DIR/test_protocol"
"$TEST_BUILD_DIR/test_ui_protocol"
"$TEST_BUILD_DIR/test_policy"
"$TEST_BUILD_DIR/test_sensors"
"$TEST_BUILD_DIR/test_downstream_status"
"$TEST_BUILD_DIR/test_json_line_accumulator"
"$TEST_BUILD_DIR/test_telemetry" > "$TEST_BUILD_DIR/state.json"
node -e 'JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"))' \
    "$TEST_BUILD_DIR/state.json"
echo "master telemetry json ok"
"$TEST_BUILD_DIR/test_master_app_link"
