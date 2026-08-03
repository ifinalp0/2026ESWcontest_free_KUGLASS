#include "ui_protocol.h"

#include "kuglass_config.h"
#include "strict_json.h"

#include <cstring>

namespace {

enum FieldBit : uint32_t {
    FIELD_VERSION = 1U << 0,
    FIELD_TYPE = 1U << 1,
    FIELD_SEQ = 1U << 2,
    FIELD_COMMAND = 1U << 3,
    FIELD_MODE = 1U << 4,
    FIELD_DEMO_MODE = 1U << 5,
    FIELD_CHANNEL = 1U << 6,
    FIELD_TARGET_MI = 1U << 7,
    FIELD_TTL = 1U << 8,
    FIELD_ENABLE = 1U << 9,
    FIELD_FAULT = 1U << 10,
    FIELD_ENVIRONMENT = 1U << 11,
};

bool set_once(uint32_t bit, uint32_t* seen, UiProtocolError* error) {
    if ((*seen & bit) != 0U) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }
    *seen |= bit;
    return true;
}

bool parse_nullable_number(StrictJsonCursor* cursor,
                           float* value,
                           bool* has_value,
                           UiProtocolError* error) {
    *has_value = false;
    if (cursor->peek('n')) {
        if (!cursor->parse_null()) {
            *error = UiProtocolError::INVALID_FIELD;
            return false;
        }
        return true;
    }
    if (!cursor->parse_number(value)) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }
    *has_value = true;
    return true;
}

bool parse_environment(StrictJsonCursor* cursor,
                       EnvironmentPatch* patch,
                       UiProtocolError* error) {
    if (!cursor->consume('{')) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }
    if (cursor->consume('}')) {
        *error = UiProtocolError::MISSING_FIELD;
        return false;
    }

    while (true) {
        char key[48] = {};
        bool lossy = false;
        if (!cursor->parse_string(key, sizeof(key), &lossy) || lossy ||
            !cursor->consume(':')) {
            *error = UiProtocolError::INVALID_FIELD;
            return false;
        }

        uint32_t field = 0;
        float* destination = nullptr;
        if (std::strcmp(key, "internal_temp_c") == 0) {
            field = ENV_INTERNAL_TEMP;
            destination = &patch->internal_temp_c;
        } else if (std::strcmp(key, "front_left_saturation") == 0) {
            field = ENV_FRONT_LEFT_SATURATION;
            destination = &patch->front_left_saturation;
        } else if (std::strcmp(key, "front_right_saturation") == 0) {
            field = ENV_FRONT_RIGHT_SATURATION;
            destination = &patch->front_right_saturation;
        } else if (std::strcmp(key, "edge_density") == 0) {
            field = ENV_EDGE_DENSITY;
            destination = &patch->edge_density;
        } else {
            *error = UiProtocolError::INVALID_FIELD;
            return false;
        }

        if ((patch->present_mask & field) != 0U) {
            *error = UiProtocolError::INVALID_FIELD;
            return false;
        }
        bool has_value = false;
        if (!parse_nullable_number(cursor, destination, &has_value, error)) return false;
        patch->present_mask |= field;
        if (has_value) {
            patch->value_mask |= field;
        }

        if ((patch->value_mask & field) != 0U) {
            const float value = *destination;
            const bool valid = field == ENV_INTERNAL_TEMP
                ? value >= -55.0f && value <= 125.0f
                : value >= 0.0f && value <= 1.0f;
            if (!valid) {
                *error = UiProtocolError::OUT_OF_RANGE;
                return false;
            }
        }

        if (cursor->consume('}')) return true;
        if (!cursor->consume(',')) {
            *error = UiProtocolError::INVALID_FIELD;
            return false;
        }
    }
}

bool parse_vehicle_mode(const char* value, VehicleMode* mode) {
    if (std::strcmp(value, "driving") == 0) *mode = VehicleMode::DRIVING;
    else if (std::strcmp(value, "stopped") == 0) *mode = VehicleMode::STOPPED;
    else if (std::strcmp(value, "camping") == 0) *mode = VehicleMode::CAMPING;
    else if (std::strcmp(value, "parked") == 0) *mode = VehicleMode::PARKED;
    else return false;
    return true;
}

bool parse_demo_mode(const char* value, DemoMode* mode) {
    if (std::strcmp(value, "none") == 0) *mode = DemoMode::NONE;
    else if (std::strcmp(value, "hot_summer") == 0) *mode = DemoMode::HOT_SUMMER;
    else if (std::strcmp(value, "camping") == 0) *mode = DemoMode::CAMPING;
    else if (std::strcmp(value, "parked") == 0) *mode = DemoMode::PARKED;
    else if (std::strcmp(value, "camera_saturation") == 0) {
        *mode = DemoMode::CAMERA_SATURATION;
    } else return false;
    return true;
}

bool parse_command_name(const char* value, UiCommandType* type) {
    if (std::strcmp(value, "set_mode") == 0) *type = UiCommandType::SET_MODE;
    else if (std::strcmp(value, "set_demo") == 0) *type = UiCommandType::SET_DEMO;
    else if (std::strcmp(value, "manual_channel") == 0) {
        *type = UiCommandType::MANUAL_CHANNEL;
    } else if (std::strcmp(value, "return_auto") == 0) {
        *type = UiCommandType::RETURN_AUTO;
    } else if (std::strcmp(value, "reset_fault") == 0) {
        *type = UiCommandType::RESET_FAULT;
    } else if (std::strcmp(value, "set_environment") == 0) {
        *type = UiCommandType::SET_ENVIRONMENT;
    } else if (std::strcmp(value, "set_channel_fault") == 0) {
        *type = UiCommandType::SET_CHANNEL_FAULT;
    } else return false;
    return true;
}

bool require_fields(uint32_t seen, uint32_t required, UiProtocolError* error) {
    if ((seen & required) != required) {
        *error = UiProtocolError::MISSING_FIELD;
        return false;
    }
    return true;
}

}  // namespace

bool parse_ui_command_line(const char* line, UiCommand* out, UiProtocolError* error) {
    if (out == nullptr || error == nullptr) return false;
    *out = UiCommand{};
    *error = UiProtocolError::OK;
    if (line == nullptr || *line == '\0') {
        *error = UiProtocolError::EMPTY;
        return false;
    }

    StrictJsonCursor cursor(line);
    if (!cursor.consume('{')) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }

    uint32_t seen = 0;
    uint32_t version = 0;
    char message_type[24] = {};
    char command_name[32] = {};
    char mode_name[24] = {};
    char demo_name[32] = {};

    if (!cursor.consume('}')) {
        while (true) {
            char key[48] = {};
            bool lossy = false;
            if (!cursor.parse_string(key, sizeof(key), &lossy) || lossy ||
                !cursor.consume(':')) {
                *error = UiProtocolError::INVALID_FIELD;
                return false;
            }

            if (std::strcmp(key, "v") == 0) {
                if (!set_once(FIELD_VERSION, &seen, error) || !cursor.parse_u32(&version)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "type") == 0) {
                if (!set_once(FIELD_TYPE, &seen, error) ||
                    !cursor.parse_string(message_type, sizeof(message_type), &lossy) || lossy) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "seq") == 0) {
                if (!set_once(FIELD_SEQ, &seen, error) || !cursor.parse_u32(&out->seq)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
                out->has_seq = true;
            } else if (std::strcmp(key, "command") == 0) {
                if (!set_once(FIELD_COMMAND, &seen, error) ||
                    !cursor.parse_string(command_name, sizeof(command_name), &lossy) || lossy) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "mode") == 0) {
                if (!set_once(FIELD_MODE, &seen, error) ||
                    !cursor.parse_string(mode_name, sizeof(mode_name), &lossy) || lossy) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "demo_mode") == 0) {
                if (!set_once(FIELD_DEMO_MODE, &seen, error) ||
                    !cursor.parse_string(demo_name, sizeof(demo_name), &lossy) || lossy) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "channel_id") == 0) {
                uint32_t channel = 0;
                if (!set_once(FIELD_CHANNEL, &seen, error) || !cursor.parse_u32(&channel)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
                if (channel >= KUGLASS_TOTAL_CHANNELS) {
                    *error = UiProtocolError::OUT_OF_RANGE;
                    return false;
                }
                out->has_channel_id = true;
                out->channel_id = static_cast<uint8_t>(channel);
            } else if (std::strcmp(key, "target_mi") == 0) {
                if (!set_once(FIELD_TARGET_MI, &seen, error) ||
                    !cursor.parse_number(&out->target_mi)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
                if (out->target_mi < 0.0f || out->target_mi > 1.0f) {
                    *error = UiProtocolError::OUT_OF_RANGE;
                    return false;
                }
            } else if (std::strcmp(key, "ttl_ms") == 0) {
                if (!set_once(FIELD_TTL, &seen, error) || !cursor.parse_u32(&out->ttl_ms)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
                if (out->ttl_ms < 1000U || out->ttl_ms > 300000U) {
                    *error = UiProtocolError::OUT_OF_RANGE;
                    return false;
                }
            } else if (std::strcmp(key, "enable") == 0) {
                if (!set_once(FIELD_ENABLE, &seen, error) || !cursor.parse_bool(&out->enable)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "fault") == 0) {
                if (!set_once(FIELD_FAULT, &seen, error) || !cursor.parse_bool(&out->fault)) {
                    *error = UiProtocolError::INVALID_FIELD;
                    return false;
                }
            } else if (std::strcmp(key, "environment") == 0) {
                if (!set_once(FIELD_ENVIRONMENT, &seen, error) ||
                    !parse_environment(&cursor, &out->environment, error)) {
                    return false;
                }
            } else if (!cursor.skip_value()) {
                *error = UiProtocolError::INVALID_FIELD;
                return false;
            }

            if (cursor.consume('}')) break;
            if (!cursor.consume(',')) {
                *error = UiProtocolError::INVALID_FIELD;
                return false;
            }
        }
    }

    if (!cursor.at_end()) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }
    if (!require_fields(seen, FIELD_VERSION | FIELD_TYPE | FIELD_SEQ | FIELD_COMMAND, error)) {
        return false;
    }
    if (version != 1U || std::strcmp(message_type, "ui_command") != 0) {
        *error = UiProtocolError::INVALID_FIELD;
        return false;
    }
    if (!parse_command_name(command_name, &out->type)) {
        *error = UiProtocolError::UNSUPPORTED_COMMAND;
        return false;
    }

    switch (out->type) {
        case UiCommandType::SET_MODE:
            if (!require_fields(seen, FIELD_MODE, error)) return false;
            if (!parse_vehicle_mode(mode_name, &out->mode)) {
                *error = UiProtocolError::OUT_OF_RANGE;
                return false;
            }
            break;
        case UiCommandType::SET_DEMO:
            if (!require_fields(seen, FIELD_DEMO_MODE, error)) return false;
            if (!parse_demo_mode(demo_name, &out->demo_mode)) {
                *error = UiProtocolError::OUT_OF_RANGE;
                return false;
            }
            break;
        case UiCommandType::MANUAL_CHANNEL:
            if (!require_fields(seen, FIELD_CHANNEL | FIELD_TARGET_MI, error)) return false;
            break;
        case UiCommandType::RETURN_AUTO:
        case UiCommandType::RESET_FAULT:
            break;
        case UiCommandType::SET_ENVIRONMENT:
            if (!require_fields(seen, FIELD_ENVIRONMENT, error)) return false;
            break;
        case UiCommandType::SET_CHANNEL_FAULT:
            if (!require_fields(seen, FIELD_CHANNEL | FIELD_FAULT, error)) return false;
            break;
        default:
            *error = UiProtocolError::UNSUPPORTED_COMMAND;
            return false;
    }

    *error = UiProtocolError::OK;
    return true;
}

const char* ui_protocol_error_name(UiProtocolError error) {
    switch (error) {
        case UiProtocolError::OK: return "OK";
        case UiProtocolError::EMPTY: return "EMPTY";
        case UiProtocolError::MISSING_COMMAND: return "MISSING_COMMAND";
        case UiProtocolError::UNSUPPORTED_COMMAND: return "UNSUPPORTED_COMMAND";
        case UiProtocolError::MISSING_FIELD: return "MISSING_FIELD";
        case UiProtocolError::INVALID_FIELD: return "INVALID_FIELD";
        case UiProtocolError::OUT_OF_RANGE: return "OUT_OF_RANGE";
        default: return "UNKNOWN";
    }
}

const char* ui_command_type_name(UiCommandType type) {
    switch (type) {
        case UiCommandType::SET_MODE: return "set_mode";
        case UiCommandType::SET_DEMO: return "set_demo";
        case UiCommandType::MANUAL_CHANNEL: return "manual_channel";
        case UiCommandType::RETURN_AUTO: return "return_auto";
        case UiCommandType::RESET_FAULT: return "reset_fault";
        case UiCommandType::SET_ENVIRONMENT: return "set_environment";
        case UiCommandType::SET_CHANNEL_FAULT: return "set_channel_fault";
        default: return "unknown";
    }
}

const char* vehicle_mode_name(VehicleMode mode) {
    switch (mode) {
        case VehicleMode::DRIVING: return "driving";
        case VehicleMode::STOPPED: return "stopped";
        case VehicleMode::CAMPING: return "camping";
        case VehicleMode::PARKED: return "parked";
        default: return "driving";
    }
}

const char* demo_mode_name(DemoMode mode) {
    switch (mode) {
        case DemoMode::NONE: return "none";
        case DemoMode::HOT_SUMMER: return "hot_summer";
        case DemoMode::CAMPING: return "camping";
        case DemoMode::PARKED: return "parked";
        case DemoMode::CAMERA_SATURATION: return "camera_saturation";
        default: return "none";
    }
}
