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
                    default: return false;  // Contract keys/values do not use \u escapes.
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
            // Skip strings without imposing a storage limit.
            ++p_;
            bool escaped = false;
            while (*p_ != '\0') {
                const unsigned char value = static_cast<unsigned char>(*p_++);
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (value == '\\') {
                    escaped = true;
                } else if (value == '"') {
                    return true;
                } else if (value < 0x20U) {
                    return false;
                }
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

    bool have_version = false;
    bool have_type = false;
    bool have_controller = false;
    bool have_seq = false;
    bool have_channels = false;
    while (!cursor.peek('}')) {
        char key[32];
        if (!cursor.parse_string(key, sizeof(key)) || !cursor.consume(':')) {
            *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
        if (std::strcmp(key, "v") == 0) {
            if (have_version) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            uint32_t version = 0;
            if (!cursor.parse_u32(&version) || version != 1U) {
                *error = DownstreamStatusError::BAD_VERSION;
                return false;
            }
            have_version = true;
        } else if (std::strcmp(key, "type") == 0) {
            if (have_type) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            char value[16];
            if (!cursor.parse_string(value, sizeof(value)) || std::strcmp(value, "status") != 0) {
                *error = DownstreamStatusError::BAD_TYPE;
                return false;
            }
            have_type = true;
        } else if (std::strcmp(key, "controller_id") == 0) {
            if (have_controller) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            char value[8];
            if (!cursor.parse_string(value, sizeof(value)) || std::strcmp(value, "B") != 0) {
                *error = DownstreamStatusError::BAD_CONTROLLER;
                return false;
            }
            have_controller = true;
        } else if (std::strcmp(key, "seq") == 0) {
            if (have_seq) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            if (!cursor.parse_u32(&output->seq)) {
                *error = DownstreamStatusError::MISSING_SEQ;
                return false;
            }
            have_seq = true;
        } else if (std::strcmp(key, "ch") == 0) {
            if (have_channels) { *error = DownstreamStatusError::DUPLICATE_FIELD; return false; }
            if (!parse_channels(&cursor, output, error)) return false;
            have_channels = true;
        } else if (!cursor.skip_value()) {
            *error = DownstreamStatusError::BAD_JSON;
            return false;
        }
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
    if (!have_version) { *error = DownstreamStatusError::MISSING_VERSION; return false; }
    if (!have_type) { *error = DownstreamStatusError::MISSING_TYPE; return false; }
    if (!have_controller) { *error = DownstreamStatusError::MISSING_CONTROLLER; return false; }
    if (!have_seq) { *error = DownstreamStatusError::MISSING_SEQ; return false; }
    if (!have_channels) { *error = DownstreamStatusError::MISSING_CHANNELS; return false; }
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
        case DownstreamStatusError::MISSING_CHANNELS: return "MISSING_CHANNELS";
        case DownstreamStatusError::BAD_CHANNEL: return "BAD_CHANNEL";
        case DownstreamStatusError::DUPLICATE_CHANNEL: return "DUPLICATE_CHANNEL";
        case DownstreamStatusError::INCOMPLETE_CHANNELS: return "INCOMPLETE_CHANNELS";
        case DownstreamStatusError::DUPLICATE_FIELD: return "DUPLICATE_FIELD";
        default: return "UNKNOWN";
    }
}
