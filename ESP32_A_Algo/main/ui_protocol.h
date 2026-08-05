#pragma once

#include <cstdint>

enum class VehicleMode : uint8_t {
    DRIVING = 0,
    STOPPED,
    CAMPING,
    PARKED,
};

enum class DemoMode : uint8_t {
    NONE = 0,
    HOT_SUMMER,
    CAMPING,
    PARKED,
    CAMERA_SATURATION,
};

enum class UiCommandType : uint8_t {
    SET_MODE = 0,
    SET_DEMO,
    MANUAL_CHANNEL,
    RETURN_AUTO,
    RESET_FAULT,
    SET_ENVIRONMENT,
    SET_CHANNEL_FAULT,
    CAMERA_STREAM,
};

enum EnvironmentField : uint32_t {
    ENV_INTERNAL_TEMP = 1U << 0,
    ENV_FRONT_LEFT_SATURATION = 1U << 1,
    ENV_FRONT_RIGHT_SATURATION = 1U << 2,
    ENV_EDGE_DENSITY = 1U << 3,
};

struct EnvironmentPatch {
    uint32_t present_mask = 0;
    uint32_t value_mask = 0;  // A present field without this bit is JSON null.
    float internal_temp_c = 0.0f;
    float front_left_saturation = 0.0f;
    float front_right_saturation = 0.0f;
    float edge_density = 0.0f;
};

struct UiCommand {
    UiCommandType type = UiCommandType::RETURN_AUTO;
    bool has_seq = false;
    uint32_t seq = 0;
    VehicleMode mode = VehicleMode::DRIVING;
    DemoMode demo_mode = DemoMode::NONE;
    bool has_channel_id = false;
    uint8_t channel_id = 0;
    float target_mi = 0.0f;
    uint32_t ttl_ms = 15000;
    bool enable = true;
    bool fault = false;
    EnvironmentPatch environment;
};

enum class UiProtocolError : uint8_t {
    OK = 0,
    EMPTY,
    MISSING_COMMAND,
    UNSUPPORTED_COMMAND,
    MISSING_FIELD,
    INVALID_FIELD,
    OUT_OF_RANGE,
};

bool parse_ui_command_line(const char* line, UiCommand* out, UiProtocolError* error);
const char* ui_protocol_error_name(UiProtocolError error);
const char* ui_command_type_name(UiCommandType type);
const char* vehicle_mode_name(VehicleMode mode);
const char* demo_mode_name(DemoMode mode);
