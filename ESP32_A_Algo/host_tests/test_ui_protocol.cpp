#include "ui_protocol.h"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    UiCommand command;
    UiProtocolError error;

    assert(parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":10,\"command\":\"set_mode\",\"mode\":\"stopped\"}",
        &command, &error));
    assert(command.type == UiCommandType::SET_MODE);
    assert(command.mode == VehicleMode::STOPPED);

    assert(parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":11,\"command\":\"manual_channel\","
        "\"channel_id\":3,\"target_mi\":0.315,\"ttl_ms\":30000,\"enable\":false}",
        &command, &error));
    assert(command.channel_id == 3);
    assert(std::fabs(command.target_mi - 0.315f) < 0.0001f);
    assert(!command.enable);

    assert(parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":12,\"command\":\"set_demo\","
        "\"demo_mode\":\"camera_saturation\"}", &command, &error));
    assert(command.demo_mode == DemoMode::CAMERA_SATURATION);

    assert(parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":13,\"command\":\"set_environment\","
        "\"environment\":{\"internal_temp_c\":37.2,\"front_left_saturation\":0.8,"
        "\"edge_density\":null}}", &command, &error));
    assert((command.environment.value_mask & ENV_INTERNAL_TEMP) != 0U);
    assert((command.environment.value_mask & ENV_FRONT_LEFT_SATURATION) != 0U);
    assert((command.environment.present_mask & ENV_EDGE_DENSITY) != 0U);
    assert((command.environment.value_mask & ENV_EDGE_DENSITY) == 0U);

    assert(!parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":14,\"command\":\"manual_channel\","
        "\"channel_id\":4,\"target_mi\":0.5}", &command, &error));
    assert(error == UiProtocolError::OUT_OF_RANGE);

    assert(!parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":15,\"command\":\"set_demo\","
        "\"demo_mode\":\"unsupported_demo\"}", &command, &error));
    assert(error == UiProtocolError::OUT_OF_RANGE);

    assert(!parse_ui_command_line(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":16,\"command\":\"set_environment\","
        "\"environment\":{\"unsupported_field\":25}}", &command, &error));
    assert(error == UiProtocolError::INVALID_FIELD);

    assert(!parse_ui_command_line(
        "{\"type\":\"ui_command\",\"seq\":17,\"command\":\"return_auto\"}",
        &command, &error));
    assert(error == UiProtocolError::MISSING_FIELD);

    assert(!parse_ui_command_line(
        "{\"v\":1,\"type\":\"status\",\"seq\":18,\"command\":\"return_auto\"}",
        &command, &error));
    assert(error == UiProtocolError::INVALID_FIELD);

    std::puts("ui protocol parser ok");
    return 0;
}
