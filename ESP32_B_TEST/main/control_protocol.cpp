#include "control_protocol.h"

#include "strict_json.h"

#include <cstring>

bool parse_reset_fault_line(const char* line,
                            ResetFaultCommand* output,
                            ControlProtocolError* error) {
    if (output == nullptr || error == nullptr) return false;
    *output = ResetFaultCommand{};
    *error = ControlProtocolError::OK;
    if (line == nullptr) {
        *error = ControlProtocolError::EMPTY;
        return false;
    }

    StrictJsonCursor cursor(line);
    if (cursor.at_end()) {
        *error = ControlProtocolError::EMPTY;
        return false;
    }
    if (!cursor.consume('{')) {
        *error = ControlProtocolError::BAD_JSON;
        return false;
    }

    bool have_version = false;
    bool have_type = false;
    bool have_seq = false;
    bool have_source_session_id = false;
    bool have_target_boot_id = false;
    bool have_reset_challenge = false;
    bool have_command = false;
    while (!cursor.peek('}')) {
        char key[32] = {};
        bool lossy_key = false;
        if (!cursor.parse_string(key, sizeof(key), &lossy_key) ||
            lossy_key || !cursor.consume(':')) {
            *error = ControlProtocolError::BAD_JSON;
            return false;
        }

        if (std::strcmp(key, "v") == 0) {
            if (have_version) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            uint32_t version = 0;
            if (!cursor.parse_u32(&version) || version != 1U) {
                *error = ControlProtocolError::BAD_VERSION;
                return false;
            }
            have_version = true;
        } else if (std::strcmp(key, "type") == 0) {
            if (have_type) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            char value[16] = {};
            bool lossy_value = false;
            if (!cursor.parse_string(value, sizeof(value), &lossy_value) ||
                lossy_value || std::strcmp(value, "control") != 0) {
                *error = ControlProtocolError::BAD_TYPE;
                return false;
            }
            have_type = true;
        } else if (std::strcmp(key, "seq") == 0) {
            if (have_seq) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            if (!cursor.parse_u32(&output->seq)) {
                *error = ControlProtocolError::BAD_SEQ;
                return false;
            }
            have_seq = true;
        } else if (std::strcmp(key, "source_session_id") == 0) {
            if (have_source_session_id) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            if (!cursor.parse_u32(&output->source_session_id) ||
                output->source_session_id == 0U) {
                *error = ControlProtocolError::BAD_SOURCE_SESSION_ID;
                return false;
            }
            have_source_session_id = true;
        } else if (std::strcmp(key, "target_boot_id") == 0) {
            if (have_target_boot_id) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            if (!cursor.parse_u32(&output->target_boot_id) ||
                output->target_boot_id == 0U) {
                *error = ControlProtocolError::BAD_TARGET_BOOT_ID;
                return false;
            }
            have_target_boot_id = true;
        } else if (std::strcmp(key, "reset_challenge") == 0) {
            if (have_reset_challenge) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            if (!cursor.parse_u32(&output->reset_challenge) ||
                output->reset_challenge == 0U) {
                *error = ControlProtocolError::BAD_RESET_CHALLENGE;
                return false;
            }
            have_reset_challenge = true;
        } else if (std::strcmp(key, "command") == 0) {
            if (have_command) {
                *error = ControlProtocolError::DUPLICATE_FIELD;
                return false;
            }
            char value[24] = {};
            bool lossy_value = false;
            if (!cursor.parse_string(value, sizeof(value), &lossy_value) ||
                lossy_value || std::strcmp(value, "reset_fault") != 0) {
                *error = ControlProtocolError::BAD_COMMAND;
                return false;
            }
            have_command = true;
        } else if (!cursor.skip_value()) {
            *error = ControlProtocolError::BAD_JSON;
            return false;
        }

        if (cursor.consume('}')) break;
        if (!cursor.consume(',')) {
            *error = ControlProtocolError::BAD_JSON;
            return false;
        }
    }
    if (cursor.peek('}') && !cursor.consume('}')) {
        *error = ControlProtocolError::BAD_JSON;
        return false;
    }
    if (!cursor.at_end()) {
        *error = ControlProtocolError::BAD_JSON;
        return false;
    }
    if (!have_version) {
        *error = ControlProtocolError::MISSING_VERSION;
        return false;
    }
    if (!have_type) {
        *error = ControlProtocolError::BAD_TYPE;
        return false;
    }
    if (!have_seq) {
        *error = ControlProtocolError::MISSING_SEQ;
        return false;
    }
    if (!have_source_session_id) {
        *error = ControlProtocolError::MISSING_SOURCE_SESSION_ID;
        return false;
    }
    if (!have_target_boot_id) {
        *error = ControlProtocolError::MISSING_TARGET_BOOT_ID;
        return false;
    }
    if (!have_reset_challenge) {
        *error = ControlProtocolError::MISSING_RESET_CHALLENGE;
        return false;
    }
    if (!have_command) {
        *error = ControlProtocolError::BAD_COMMAND;
        return false;
    }
    return true;
}

const char* control_protocol_error_name(ControlProtocolError error) {
    switch (error) {
        case ControlProtocolError::OK: return "OK";
        case ControlProtocolError::EMPTY: return "EMPTY";
        case ControlProtocolError::BAD_JSON: return "BAD_JSON";
        case ControlProtocolError::DUPLICATE_FIELD: return "DUPLICATE_FIELD";
        case ControlProtocolError::BAD_VERSION: return "BAD_VERSION";
        case ControlProtocolError::BAD_TYPE: return "BAD_TYPE";
        case ControlProtocolError::MISSING_VERSION: return "MISSING_VERSION";
        case ControlProtocolError::MISSING_SEQ: return "MISSING_SEQ";
        case ControlProtocolError::BAD_SEQ: return "BAD_SEQ";
        case ControlProtocolError::MISSING_SOURCE_SESSION_ID:
            return "MISSING_SOURCE_SESSION_ID";
        case ControlProtocolError::BAD_SOURCE_SESSION_ID:
            return "BAD_SOURCE_SESSION_ID";
        case ControlProtocolError::MISSING_TARGET_BOOT_ID:
            return "MISSING_TARGET_BOOT_ID";
        case ControlProtocolError::BAD_TARGET_BOOT_ID:
            return "BAD_TARGET_BOOT_ID";
        case ControlProtocolError::MISSING_RESET_CHALLENGE:
            return "MISSING_RESET_CHALLENGE";
        case ControlProtocolError::BAD_RESET_CHALLENGE:
            return "BAD_RESET_CHALLENGE";
        case ControlProtocolError::BAD_COMMAND: return "BAD_COMMAND";
        default: return "UNKNOWN";
    }
}
