#include "protocol.h"
#include "kuglass_config.h"
#include "strict_json.h"

#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t kExpectedChannelMask =
    static_cast<uint8_t>((1U << KUGLASS_MAX_COMMAND_CHANNELS) - 1U);

bool append_text(char* output, size_t output_size, size_t* used, const char* format, ...) {
    if (output == nullptr || used == nullptr || *used >= output_size) {
        return false;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(output + *used, output_size - *used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= output_size - *used) {
        output[output_size - 1] = '\0';
        return false;
    }
    *used += static_cast<size_t>(written);
    return true;
}

bool parse_channel_array(StrictJsonCursor* cursor,
                         ProtocolCommand* out,
                         ProtocolError* error) {
    if (!cursor->consume('[')) {
        *error = ProtocolError::BAD_CHANNEL_ARRAY;
        return false;
    }
    out->channel_count = 0;
    uint8_t seen = 0;
    if (cursor->consume(']')) {
        *error = ProtocolError::BAD_CHANNEL_ARRAY;
        return false;
    }
    while (true) {
        if (!cursor->consume('[')) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        if (out->channel_count >= KUGLASS_MAX_COMMAND_CHANNELS) {
            *error = ProtocolError::TOO_MANY_CHANNELS;
            return false;
        }
        uint32_t channel_id = 0;
        if (!cursor->parse_u32(&channel_id) ||
            channel_id >= KUGLASS_MAX_COMMAND_CHANNELS || !cursor->consume(',')) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        const uint8_t channel_bit = static_cast<uint8_t>(1U << channel_id);
        if ((seen & channel_bit) != 0U) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        float mi = 0.0f;
        if (!cursor->parse_number(&mi) || mi < 0.0f ||
            mi > KUGLASS_MAX_MODULATION_INDEX ||
            !cursor->consume(',')) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        bool enable = false;
        if (!cursor->parse_bool(&enable) || !cursor->consume(']')) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        ProtocolChannelCommand item;
        item.channel_id = static_cast<uint8_t>(channel_id);
        item.mi = mi;
        item.enable = enable;
        out->channels[out->channel_count++] = item;
        seen |= channel_bit;
        if (cursor->consume(']')) break;
        if (!cursor->consume(',')) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
    }
    if (out->channel_count != KUGLASS_MAX_COMMAND_CHANNELS ||
        seen != kExpectedChannelMask) {
        *error = ProtocolError::BAD_CHANNEL_ARRAY;
        return false;
    }
    *error = ProtocolError::OK;
    return true;
}

}  // namespace

bool parse_command_line(const char* line, ProtocolCommand* out, ProtocolError* error) {
    if (out == nullptr || error == nullptr) {
        return false;
    }
    *out = ProtocolCommand{};
    *error = ProtocolError::OK;
    if (line == nullptr) {
        *error = ProtocolError::EMPTY;
        return false;
    }
    StrictJsonCursor cursor(line);
    if (cursor.at_end()) {
        *error = ProtocolError::EMPTY;
        return false;
    }
    if (!cursor.consume('{')) {
        *error = ProtocolError::BAD_JSON;
        return false;
    }

    bool have_version = false;
    bool have_type = false;
    bool have_seq = false;
    bool have_ttl = false;
    bool have_channels = false;
    uint32_t version = 0;

    if (!cursor.consume('}')) {
        while (true) {
            char key[32] = {};
            bool lossy_key = false;
            if (!cursor.parse_string(key, sizeof(key), &lossy_key) ||
                !cursor.consume(':')) {
                *error = ProtocolError::BAD_JSON;
                return false;
            }

            if (!lossy_key && std::strcmp(key, "v") == 0) {
                if (have_version) { *error = ProtocolError::DUPLICATE_FIELD; return false; }
                if (!cursor.parse_u32(&version)) { *error = ProtocolError::BAD_VERSION; return false; }
                have_version = true;
            } else if (!lossy_key && std::strcmp(key, "type") == 0) {
                if (have_type) { *error = ProtocolError::DUPLICATE_FIELD; return false; }
                char value[32] = {};
                bool lossy_value = false;
                if (!cursor.parse_string(value, sizeof(value), &lossy_value) || lossy_value ||
                    std::strcmp(value, "actuator_command") != 0) {
                    *error = ProtocolError::BAD_TYPE;
                    return false;
                }
                have_type = true;
            } else if (!lossy_key && std::strcmp(key, "seq") == 0) {
                if (have_seq) { *error = ProtocolError::DUPLICATE_FIELD; return false; }
                if (!cursor.parse_u32(&out->seq)) { *error = ProtocolError::BAD_SEQ; return false; }
                have_seq = true;
            } else if (!lossy_key && std::strcmp(key, "ttl_ms") == 0) {
                if (have_ttl) { *error = ProtocolError::DUPLICATE_FIELD; return false; }
                if (!cursor.parse_u32(&out->ttl_ms)) { *error = ProtocolError::BAD_TTL; return false; }
                have_ttl = true;
            } else if (!lossy_key && std::strcmp(key, "ch") == 0) {
                if (have_channels) { *error = ProtocolError::DUPLICATE_FIELD; return false; }
                if (!parse_channel_array(&cursor, out, error)) return false;
                have_channels = true;
            } else if (!cursor.skip_value()) {
                *error = ProtocolError::BAD_JSON;
                return false;
            }

            if (cursor.consume('}')) break;
            if (!cursor.consume(',')) {
                *error = ProtocolError::BAD_JSON;
                return false;
            }
        }
    }
    if (!cursor.at_end()) { *error = ProtocolError::BAD_JSON; return false; }
    if (!have_version) { *error = ProtocolError::MISSING_VERSION; return false; }
    if (version != 1U) { *error = ProtocolError::BAD_VERSION; return false; }
    if (!have_type) { *error = ProtocolError::MISSING_TYPE; return false; }
    if (!have_seq) { *error = ProtocolError::MISSING_SEQ; return false; }
    if (!have_ttl) { *error = ProtocolError::MISSING_TTL; return false; }
    if (!have_channels) { *error = ProtocolError::MISSING_CHANNELS; return false; }
    if (out->ttl_ms < 50U || out->ttl_ms > 1000U) {
        *error = ProtocolError::BAD_TTL;
        return false;
    }
    *error = ProtocolError::OK;
    return true;
}

bool format_command_line(const ProtocolCommand& command, char* output, size_t output_size) {
    if (output == nullptr || output_size == 0 ||
        command.channel_count != KUGLASS_MAX_COMMAND_CHANNELS) {
        return false;
    }
    uint8_t seen = 0;
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& channel = command.channels[i];
        if (channel.channel_id >= KUGLASS_MAX_COMMAND_CHANNELS ||
            !std::isfinite(channel.mi) || channel.mi < 0.0f ||
            channel.mi > KUGLASS_MAX_MODULATION_INDEX) {
            return false;
        }
        const uint8_t bit = static_cast<uint8_t>(1U << channel.channel_id);
        if ((seen & bit) != 0U) return false;
        seen |= bit;
    }
    if (seen != kExpectedChannelMask) return false;
    output[0] = '\0';
    size_t used = 0;
    if (!append_text(output, output_size, &used,
                     "{\"v\":1,\"type\":\"actuator_command\","
                     "\"seq\":%lu,\"ttl_ms\":%lu,\"ch\":[",
                     static_cast<unsigned long>(command.seq),
                     static_cast<unsigned long>(command.ttl_ms))) {
        return false;
    }
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& channel = command.channels[i];
        if (!append_text(output, output_size, &used,
                         "%s[%u,%.4f,%s]",
                         i == 0 ? "" : ",",
                         static_cast<unsigned>(channel.channel_id),
                         channel.mi,
                         channel.enable ? "true" : "false")) {
            return false;
        }
    }
    return append_text(output, output_size, &used, "]}");
}

const char* protocol_error_name(ProtocolError error) {
    switch (error) {
        case ProtocolError::OK:
            return "OK";
        case ProtocolError::EMPTY:
            return "EMPTY";
        case ProtocolError::BAD_JSON:
            return "BAD_JSON";
        case ProtocolError::DUPLICATE_FIELD:
            return "DUPLICATE_FIELD";
        case ProtocolError::MISSING_VERSION:
            return "MISSING_VERSION";
        case ProtocolError::BAD_VERSION:
            return "BAD_VERSION";
        case ProtocolError::MISSING_TYPE:
            return "MISSING_TYPE";
        case ProtocolError::BAD_TYPE:
            return "BAD_TYPE";
        case ProtocolError::MISSING_SEQ:
            return "MISSING_SEQ";
        case ProtocolError::BAD_SEQ:
            return "BAD_SEQ";
        case ProtocolError::MISSING_TTL:
            return "MISSING_TTL";
        case ProtocolError::BAD_TTL:
            return "BAD_TTL";
        case ProtocolError::MISSING_CHANNELS:
            return "MISSING_CHANNELS";
        case ProtocolError::BAD_CHANNEL_ARRAY:
            return "BAD_CHANNEL_ARRAY";
        case ProtocolError::TOO_MANY_CHANNELS:
            return "TOO_MANY_CHANNELS";
        default:
            return "UNKNOWN";
    }
}
