#include "downstream_status.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

static_assert(KUGLASS_TOTAL_CHANNELS == 4,
              "The ESP32_B status contract requires exactly four channels.");
constexpr uint8_t kExpectedChannelMask =
    static_cast<uint8_t>((1U << KUGLASS_TOTAL_CHANNELS) - 1U);

class JsonCursor {
public:
    explicit JsonCursor(const char* input) : p_(input) {}

    void skip_ws() {
        while (p_ != nullptr && *p_ != '\0' &&
               std::isspace(static_cast<unsigned char>(*p_))) {
            ++p_;
        }
    }

    bool consume(char expected) {
        skip_ws();
        if (p_ == nullptr || *p_ != expected) return false;
        ++p_;
        return true;
    }

    bool peek(char expected) {
        skip_ws();
        return p_ != nullptr && *p_ == expected;
    }

    bool at_end() {
        skip_ws();
        return p_ != nullptr && *p_ == '\0';
    }

    bool parse_string(char* output, size_t output_size) {
        skip_ws();
        if (p_ == nullptr || *p_ != '"' || output == nullptr || output_size == 0) {
            return false;
        }
        ++p_;
        size_t used = 0;
        while (*p_ != '\0' && *p_ != '"') {
            unsigned char value = static_cast<unsigned char>(*p_++);
            if (value < 0x20U) return false;
            if (value == '\\') {
                const char escaped = *p_++;
                if (escaped == '\0') return false;
                switch (escaped) {
                    case '"': value = '"'; break;
                    case '\\': value = '\\'; break;
                    case '/': value = '/'; break;
                    case 'b': value = '\b'; break;
                    case 'f': value = '\f'; break;
                    case 'n': value = '\n'; break;
                    case 'r': value = '\r'; break;
                    case 't': value = '\t'; break;
                    default: return false;
                }
            }
            if (used + 1 >= output_size) return false;
            output[used++] = static_cast<char>(value);
        }
        if (*p_ != '"') return false;
        ++p_;
        output[used] = '\0';
        return true;
    }

    bool parse_u32(uint32_t* output) {
        skip_ws();
        if (p_ == nullptr || output == nullptr || *p_ == '-' || *p_ == '+') return false;
        char* end = nullptr;
        const unsigned long value = std::strtoul(p_, &end, 10);
        if (end == p_ || value > UINT32_MAX || !is_value_end(end)) return false;
        p_ = end;
        *output = static_cast<uint32_t>(value);
        return true;
    }

    bool parse_float(float* output) {
        skip_ws();
        if (p_ == nullptr || output == nullptr || *p_ == '+') return false;
        char* end = nullptr;
        const float value = std::strtof(p_, &end);
        if (end == p_ || !std::isfinite(value) || !is_value_end(end)) return false;
        p_ = end;
        *output = value;
        return true;
    }

    bool parse_bool(bool* output) {
        skip_ws();
        if (p_ == nullptr || output == nullptr) return false;
        if (std::strncmp(p_, "true", 4) == 0 && is_value_end(p_ + 4)) {
            p_ += 4;
            *output = true;
            return true;
        }
        if (std::strncmp(p_, "false", 5) == 0 && is_value_end(p_ + 5)) {
            p_ += 5;
            *output = false;
            return true;
        }
        return false;
    }

    bool skip_value(unsigned depth = 0) {
        if (depth > 8) return false;
        skip_ws();
        if (p_ == nullptr) return false;
        if (*p_ == '"') {
            ++p_;
            bool escaped = false;
            while (*p_ != '\0') {
                const unsigned char value = static_cast<unsigned char>(*p_++);
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (value == '\\') escaped = true;
                else if (value == '"') return true;
                else if (value < 0x20U) return false;
            }
            return false;
        }
        if (*p_ == '{') {
            ++p_;
            skip_ws();
            if (*p_ == '}') { ++p_; return true; }
            while (true) {
                char key[32];
                if (!parse_string(key, sizeof(key)) || !consume(':') ||
                    !skip_value(depth + 1)) return false;
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (*p_ == '[') {
            ++p_;
            skip_ws();
            if (*p_ == ']') { ++p_; return true; }
            while (true) {
                if (!skip_value(depth + 1)) return false;
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (std::strncmp(p_, "true", 4) == 0 && is_value_end(p_ + 4)) {
            p_ += 4; return true;
        }
        if (std::strncmp(p_, "false", 5) == 0 && is_value_end(p_ + 5)) {
            p_ += 5; return true;
        }
        if (std::strncmp(p_, "null", 4) == 0 && is_value_end(p_ + 4)) {
            p_ += 4; return true;
        }
        char* end = nullptr;
        const double number = std::strtod(p_, &end);
        if (end == p_ || !std::isfinite(number) || !is_value_end(end)) return false;
        p_ = end;
        return true;
    }

private:
    bool is_value_end(const char* end) const {
        while (end != nullptr && *end != '\0' &&
               std::isspace(static_cast<unsigned char>(*end))) {
            ++end;
        }
        return end != nullptr &&
               (*end == '\0' || *end == ',' || *end == '}' || *end == ']');
    }

    const char* p_;
};

bool token_is_safe(const char* value) {
    if (value == nullptr || value[0] == '\0') return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value);
         *p != '\0'; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '_')) return false;
    }
    return true;
}

bool parse_fault_code(const char* value, DownstreamFaultCode* output) {
    if (std::strcmp(value, "NONE") == 0) *output = DownstreamFaultCode::NONE;
    else if (std::strcmp(value, "COMM_TIMEOUT") == 0) {
        *output = DownstreamFaultCode::COMM_TIMEOUT;
    } else if (std::strcmp(value, "INVALID_COMMAND") == 0) {
        *output = DownstreamFaultCode::INVALID_COMMAND;
    } else if (std::strcmp(value, "POWER_STAGE_FAULT") == 0) {
        *output = DownstreamFaultCode::POWER_STAGE_FAULT;
    } else if (std::strcmp(value, "ESTOP") == 0) {
        *output = DownstreamFaultCode::ESTOP;
    } else {
        return false;
    }
    return true;
}

bool parse_control_error(const char* value,
                         DownstreamControlResultError* output) {
    if (std::strcmp(value, "NONE") == 0) {
        *output = DownstreamControlResultError::NONE;
    } else if (std::strcmp(value, "RESET_UNSAFE") == 0) {
        *output = DownstreamControlResultError::RESET_UNSAFE;
    } else if (std::strcmp(value, "TARGET_BOOT_MISMATCH") == 0) {
        *output = DownstreamControlResultError::TARGET_BOOT_MISMATCH;
    } else if (std::strcmp(value, "CHALLENGE_MISMATCH") == 0) {
        *output = DownstreamControlResultError::CHALLENGE_MISMATCH;
    } else {
        return false;
    }
    return true;
}

bool parse_u16_array(JsonCursor* cursor, uint16_t* output, uint32_t maximum) {
    if (!cursor->consume('[')) return false;
    for (size_t i = 0; i < KUGLASS_TOTAL_CHANNELS; ++i) {
        uint32_t value = 0;
        if (!cursor->parse_u32(&value) || value > maximum) return false;
        output[i] = static_cast<uint16_t>(value);
        if (i + 1 < KUGLASS_TOTAL_CHANNELS) {
            if (!cursor->consume(',')) return false;
        }
    }
    return cursor->consume(']');
}

bool parse_channel(JsonCursor* cursor,
                   DownstreamChannelStatus* channel,
                   DownstreamStatusError* error) {
    if (!cursor->consume('{')) {
        *error = DownstreamStatusError::BAD_CHANNEL;
        return false;
    }
    bool have_id = false;
    bool have_mi = false;
    bool have_fault = false;
    while (!cursor->peek('}')) {
        char key[32];
        if (!cursor->parse_string(key, sizeof(key)) || !cursor->consume(':')) {
            *error = DownstreamStatusError::BAD_CHANNEL;
            return false;
        }
        if (std::strcmp(key, "id") == 0) {
            if (have_id) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            uint32_t id = 0;
            if (!cursor->parse_u32(&id) || id >= KUGLASS_TOTAL_CHANNELS) {
                *error = DownstreamStatusError::BAD_CHANNEL;
                return false;
            }
            channel->channel_id = static_cast<uint8_t>(id);
            have_id = true;
        } else if (std::strcmp(key, "mi") == 0) {
            if (have_mi) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            if (!cursor->parse_float(&channel->applied_mi) ||
                channel->applied_mi < 0.0f || channel->applied_mi > 1.0f) {
                *error = DownstreamStatusError::BAD_CHANNEL;
                return false;
            }
            have_mi = true;
        } else if (std::strcmp(key, "fault") == 0) {
            if (have_fault) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            if (!cursor->parse_bool(&channel->fault)) {
                *error = DownstreamStatusError::BAD_CHANNEL;
                return false;
            }
            have_fault = true;
        } else if (!cursor->skip_value()) {
            *error = DownstreamStatusError::BAD_CHANNEL;
            return false;
        }
        if (cursor->consume('}')) break;
        if (!cursor->consume(',')) {
            *error = DownstreamStatusError::BAD_CHANNEL;
            return false;
        }
    }
    if (cursor->peek('}') && !cursor->consume('}')) {
        *error = DownstreamStatusError::BAD_CHANNEL;
        return false;
    }
    if (!have_id || !have_mi || !have_fault) {
        *error = DownstreamStatusError::BAD_CHANNEL;
        return false;
    }
    return true;
}

bool parse_channels(JsonCursor* cursor,
                    DownstreamStatus* output,
                    DownstreamStatusError* error) {
    if (!cursor->consume('[')) {
        *error = DownstreamStatusError::MISSING_CHANNELS;
        return false;
    }
    size_t count = 0;
    uint8_t seen = 0;
    while (!cursor->peek(']')) {
        if (count >= KUGLASS_TOTAL_CHANNELS) {
            *error = DownstreamStatusError::INCOMPLETE_CHANNELS;
            return false;
        }
        DownstreamChannelStatus channel;
        if (!parse_channel(cursor, &channel, error)) return false;
        const uint8_t bit = static_cast<uint8_t>(1U << channel.channel_id);
        if ((seen & bit) != 0U) {
            *error = DownstreamStatusError::DUPLICATE_CHANNEL;
            return false;
        }
        seen |= bit;
        output->channels[channel.channel_id] = channel;
        ++count;
        if (cursor->consume(']')) break;
        if (!cursor->consume(',')) {
            *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
    }
    if (cursor->peek(']') && !cursor->consume(']')) {
        *error = DownstreamStatusError::BAD_JSON;
        return false;
    }
    if (count != KUGLASS_TOTAL_CHANNELS || seen != kExpectedChannelMask) {
        *error = DownstreamStatusError::INCOMPLETE_CHANNELS;
        return false;
    }
    return true;
}

bool parse_adc(JsonCursor* cursor, DownstreamAdcStatus* output) {
    if (!cursor->consume('{')) return false;
    uint16_t seen = 0;
    while (!cursor->peek('}')) {
        char key[32];
        if (!cursor->parse_string(key, sizeof(key)) || !cursor->consume(':')) return false;
        uint16_t bit = 0;
        bool parsed = false;
        if (std::strcmp(key, "initialized") == 0) {
            bit = 1U << 0; parsed = cursor->parse_bool(&output->initialized);
        } else if (std::strcmp(key, "i_cali") == 0) {
            bit = 1U << 1; parsed = cursor->parse_bool(&output->current_calibrated);
        } else if (std::strcmp(key, "t_cali") == 0) {
            bit = 1U << 2; parsed = cursor->parse_bool(&output->temperature_calibrated);
        } else if (std::strcmp(key, "raw_valid_mask") == 0 ||
                   std::strcmp(key, "mv_valid_mask") == 0) {
            bit = std::strcmp(key, "raw_valid_mask") == 0 ? 1U << 3 : 1U << 4;
            uint32_t value = 0;
            parsed = cursor->parse_u32(&value) && value <= UINT8_MAX;
            if (parsed) {
                if (bit == (1U << 3)) output->raw_valid_mask = static_cast<uint8_t>(value);
                else output->mv_valid_mask = static_cast<uint8_t>(value);
            }
        } else if (std::strcmp(key, "i_raw") == 0) {
            bit = 1U << 5; parsed = parse_u16_array(cursor, output->current_raw, 4095);
        } else if (std::strcmp(key, "t_raw") == 0) {
            bit = 1U << 6; parsed = parse_u16_array(cursor, output->temperature_raw, 4095);
        } else if (std::strcmp(key, "i_mv") == 0) {
            bit = 1U << 7; parsed = parse_u16_array(cursor, output->current_mv, 5000);
        } else if (std::strcmp(key, "t_mv") == 0) {
            bit = 1U << 8; parsed = parse_u16_array(cursor, output->temperature_mv, 5000);
        } else {
            parsed = cursor->skip_value();
        }
        if (!parsed || (bit != 0U && (seen & bit) != 0U)) return false;
        seen |= bit;
        if (cursor->consume('}')) break;
        if (!cursor->consume(',')) return false;
    }
    if (cursor->peek('}') && !cursor->consume('}')) return false;
    return seen == 0x01FFU;
}

bool parse_control_result(JsonCursor* cursor, DownstreamControlResult* output) {
    if (!cursor->consume('{')) return false;
    uint8_t seen = 0;
    while (!cursor->peek('}')) {
        char key[32];
        if (!cursor->parse_string(key, sizeof(key)) || !cursor->consume(':')) return false;
        uint8_t bit = 0;
        bool parsed = false;
        if (std::strcmp(key, "command") == 0) {
            bit = 1U << 0;
            char value[24];
            parsed = cursor->parse_string(value, sizeof(value)) &&
                std::strcmp(value, "reset_fault") == 0;
        } else if (std::strcmp(key, "seq") == 0) {
            bit = 1U << 1; parsed = cursor->parse_u32(&output->seq);
        } else if (std::strcmp(key, "source_session_id") == 0) {
            bit = 1U << 2;
            parsed = cursor->parse_u32(&output->source_session_id) &&
                output->source_session_id != 0U;
        } else if (std::strcmp(key, "ok") == 0) {
            bit = 1U << 3; parsed = cursor->parse_bool(&output->ok);
        } else if (std::strcmp(key, "error") == 0) {
            bit = 1U << 4;
            char value[32];
            parsed = cursor->parse_string(value, sizeof(value)) &&
                parse_control_error(value, &output->error);
        } else {
            parsed = cursor->skip_value();
        }
        if (!parsed || (bit != 0U && (seen & bit) != 0U)) return false;
        seen |= bit;
        if (cursor->consume('}')) break;
        if (!cursor->consume(',')) return false;
    }
    if (cursor->peek('}') && !cursor->consume('}')) return false;
    if (seen != 0x1FU) return false;
    return output->ok
        ? output->error == DownstreamControlResultError::NONE
        : output->error != DownstreamControlResultError::NONE;
}

}  // namespace

bool parse_downstream_status_line(const char* line,
                                  DownstreamStatus* output,
                                  DownstreamStatusError* error) {
    if (output == nullptr || error == nullptr) return false;
    *output = DownstreamStatus{};
    *error = DownstreamStatusError::OK;
    if (line == nullptr) {
        *error = DownstreamStatusError::EMPTY;
        return false;
    }
    JsonCursor cursor(line);
    if (!cursor.consume('{')) {
        *error = DownstreamStatusError::BAD_JSON;
        return false;
    }

    enum Field : uint16_t {
        VERSION = 1U << 0,
        TYPE = 1U << 1,
        CONTROLLER = 1U << 2,
        SEQ = 1U << 3,
        BOOT_ID = 1U << 4,
        RESET_CHALLENGE = 1U << 5,
        ESTOP = 1U << 6,
        FAULT_CODE = 1U << 7,
        CHANNELS = 1U << 8,
        DIAGNOSTIC = 1U << 9,
        ADC = 1U << 10,
        CONTROL_RESULT = 1U << 11,
    };
    uint16_t seen = 0;
    while (!cursor.peek('}')) {
        char key[32];
        if (!cursor.parse_string(key, sizeof(key)) || !cursor.consume(':')) {
            *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
        uint16_t bit = 0;
        bool parsed = false;
        if (std::strcmp(key, "v") == 0) {
            bit = VERSION;
            uint32_t version = 0;
            parsed = cursor.parse_u32(&version) && version == 1U;
            if (!parsed) *error = DownstreamStatusError::BAD_VERSION;
        } else if (std::strcmp(key, "type") == 0) {
            bit = TYPE;
            char value[16];
            parsed = cursor.parse_string(value, sizeof(value)) &&
                std::strcmp(value, "status") == 0;
            if (!parsed) *error = DownstreamStatusError::BAD_TYPE;
        } else if (std::strcmp(key, "controller_id") == 0) {
            bit = CONTROLLER;
            char value[8];
            parsed = cursor.parse_string(value, sizeof(value)) &&
                std::strcmp(value, "B") == 0;
            if (!parsed) *error = DownstreamStatusError::BAD_CONTROLLER;
        } else if (std::strcmp(key, "seq") == 0) {
            bit = SEQ;
            parsed = cursor.parse_u32(&output->seq);
            if (!parsed) *error = DownstreamStatusError::MISSING_SEQ;
        } else if (std::strcmp(key, "boot_id") == 0) {
            bit = BOOT_ID;
            parsed = cursor.parse_u32(&output->boot_id) && output->boot_id != 0U;
            if (!parsed) *error = DownstreamStatusError::BAD_BOOT_ID;
        } else if (std::strcmp(key, "reset_challenge") == 0) {
            bit = RESET_CHALLENGE;
            parsed = cursor.parse_u32(&output->reset_challenge) &&
                output->reset_challenge != 0U;
            if (!parsed) *error = DownstreamStatusError::BAD_RESET_CHALLENGE;
        } else if (std::strcmp(key, "estop") == 0) {
            bit = ESTOP;
            parsed = cursor.parse_bool(&output->estop);
            if (!parsed) *error = DownstreamStatusError::BAD_ESTOP;
        } else if (std::strcmp(key, "fault_code") == 0) {
            bit = FAULT_CODE;
            char value[32];
            parsed = cursor.parse_string(value, sizeof(value)) &&
                parse_fault_code(value, &output->fault_code);
            if (!parsed) *error = DownstreamStatusError::BAD_FAULT_CODE;
        } else if (std::strcmp(key, "diagnostic") == 0) {
            bit = DIAGNOSTIC;
            parsed = cursor.parse_string(output->diagnostic,
                                         sizeof(output->diagnostic)) &&
                token_is_safe(output->diagnostic);
            output->has_diagnostic = parsed;
            if (!parsed) *error = DownstreamStatusError::BAD_DIAGNOSTIC;
        } else if (std::strcmp(key, "ch") == 0) {
            bit = CHANNELS;
            parsed = parse_channels(&cursor, output, error);
        } else if (std::strcmp(key, "adc") == 0) {
            bit = ADC;
            parsed = parse_adc(&cursor, &output->adc);
            output->has_adc = parsed;
            if (!parsed) *error = DownstreamStatusError::BAD_ADC;
        } else if (std::strcmp(key, "control_result") == 0) {
            bit = CONTROL_RESULT;
            parsed = parse_control_result(&cursor, &output->control_result);
            output->has_control_result = parsed;
            if (!parsed) *error = DownstreamStatusError::BAD_CONTROL_RESULT;
        } else {
            parsed = cursor.skip_value();
        }
        if (!parsed) {
            if (*error == DownstreamStatusError::OK) *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
        if (bit != 0U && (seen & bit) != 0U) {
            *error = DownstreamStatusError::DUPLICATE_FIELD;
            return false;
        }
        seen |= bit;
        if (cursor.consume('}')) break;
        if (!cursor.consume(',')) {
            *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
    }
    if (cursor.peek('}') && !cursor.consume('}')) {
        *error = DownstreamStatusError::BAD_JSON;
        return false;
    }
    if (!cursor.at_end()) {
        *error = DownstreamStatusError::BAD_JSON;
        return false;
    }

    if ((seen & VERSION) == 0U) { *error = DownstreamStatusError::MISSING_VERSION; return false; }
    if ((seen & TYPE) == 0U) { *error = DownstreamStatusError::MISSING_TYPE; return false; }
    if ((seen & CONTROLLER) == 0U) { *error = DownstreamStatusError::MISSING_CONTROLLER; return false; }
    if ((seen & SEQ) == 0U) { *error = DownstreamStatusError::MISSING_SEQ; return false; }
    if ((seen & BOOT_ID) == 0U) { *error = DownstreamStatusError::MISSING_BOOT_ID; return false; }
    if ((seen & RESET_CHALLENGE) == 0U) {
        *error = DownstreamStatusError::MISSING_RESET_CHALLENGE; return false;
    }
    if ((seen & ESTOP) == 0U) { *error = DownstreamStatusError::MISSING_ESTOP; return false; }
    if ((seen & FAULT_CODE) == 0U) {
        *error = DownstreamStatusError::MISSING_FAULT_CODE; return false;
    }
    if ((seen & CHANNELS) == 0U) {
        *error = DownstreamStatusError::MISSING_CHANNELS; return false;
    }
    return true;
}

const char* downstream_status_error_name(DownstreamStatusError error) {
    switch (error) {
        case DownstreamStatusError::OK: return "OK";
        case DownstreamStatusError::EMPTY: return "EMPTY";
        case DownstreamStatusError::BAD_JSON: return "BAD_JSON";
        case DownstreamStatusError::MISSING_VERSION: return "MISSING_VERSION";
        case DownstreamStatusError::BAD_VERSION: return "BAD_VERSION";
        case DownstreamStatusError::MISSING_TYPE: return "MISSING_TYPE";
        case DownstreamStatusError::BAD_TYPE: return "BAD_TYPE";
        case DownstreamStatusError::MISSING_CONTROLLER: return "MISSING_CONTROLLER";
        case DownstreamStatusError::BAD_CONTROLLER: return "BAD_CONTROLLER";
        case DownstreamStatusError::MISSING_SEQ: return "MISSING_SEQ";
        case DownstreamStatusError::MISSING_BOOT_ID: return "MISSING_BOOT_ID";
        case DownstreamStatusError::BAD_BOOT_ID: return "BAD_BOOT_ID";
        case DownstreamStatusError::MISSING_RESET_CHALLENGE: return "MISSING_RESET_CHALLENGE";
        case DownstreamStatusError::BAD_RESET_CHALLENGE: return "BAD_RESET_CHALLENGE";
        case DownstreamStatusError::MISSING_ESTOP: return "MISSING_ESTOP";
        case DownstreamStatusError::BAD_ESTOP: return "BAD_ESTOP";
        case DownstreamStatusError::MISSING_FAULT_CODE: return "MISSING_FAULT_CODE";
        case DownstreamStatusError::BAD_FAULT_CODE: return "BAD_FAULT_CODE";
        case DownstreamStatusError::BAD_DIAGNOSTIC: return "BAD_DIAGNOSTIC";
        case DownstreamStatusError::BAD_ADC: return "BAD_ADC";
        case DownstreamStatusError::BAD_CONTROL_RESULT: return "BAD_CONTROL_RESULT";
        case DownstreamStatusError::MISSING_CHANNELS: return "MISSING_CHANNELS";
        case DownstreamStatusError::BAD_CHANNEL: return "BAD_CHANNEL";
        case DownstreamStatusError::DUPLICATE_CHANNEL: return "DUPLICATE_CHANNEL";
        case DownstreamStatusError::INCOMPLETE_CHANNELS: return "INCOMPLETE_CHANNELS";
        case DownstreamStatusError::DUPLICATE_FIELD: return "DUPLICATE_FIELD";
        default: return "UNKNOWN";
    }
}

const char* downstream_fault_code_name(DownstreamFaultCode code) {
    switch (code) {
        case DownstreamFaultCode::NONE: return "NONE";
        case DownstreamFaultCode::COMM_TIMEOUT: return "COMM_TIMEOUT";
        case DownstreamFaultCode::INVALID_COMMAND: return "INVALID_COMMAND";
        case DownstreamFaultCode::POWER_STAGE_FAULT: return "POWER_STAGE_FAULT";
        case DownstreamFaultCode::ESTOP: return "ESTOP";
        default: return "UNKNOWN";
    }
}

const char* downstream_control_result_error_name(
    DownstreamControlResultError error) {
    switch (error) {
        case DownstreamControlResultError::NONE: return "NONE";
        case DownstreamControlResultError::RESET_UNSAFE: return "RESET_UNSAFE";
        case DownstreamControlResultError::TARGET_BOOT_MISMATCH:
            return "TARGET_BOOT_MISMATCH";
        case DownstreamControlResultError::CHALLENGE_MISMATCH:
            return "CHALLENGE_MISMATCH";
        default: return "UNKNOWN";
    }
}
