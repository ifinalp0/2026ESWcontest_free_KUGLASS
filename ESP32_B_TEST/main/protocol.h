#pragma once

#include <cstddef>
#include <cstdint>

static constexpr size_t KUGLASS_MAX_COMMAND_CHANNELS = 4;

enum class ProtocolError : uint8_t {
    OK = 0,
    EMPTY,
    BAD_JSON,
    DUPLICATE_FIELD,
    MISSING_VERSION,
    BAD_VERSION,
    MISSING_TYPE,
    BAD_TYPE,
    MISSING_SEQ,
    BAD_SEQ,
    MISSING_TTL,
    BAD_TTL,
    MISSING_CHANNELS,
    BAD_CHANNEL_ARRAY,
    TOO_MANY_CHANNELS,
};

struct ProtocolChannelCommand {
    uint8_t channel_id = 0;
    float mi = 0.0f;
    bool enable = true;
};

struct ProtocolCommand {
    uint32_t seq = 0;
    uint32_t ttl_ms = 0;
    ProtocolChannelCommand channels[KUGLASS_MAX_COMMAND_CHANNELS];
    size_t channel_count = 0;
};

bool parse_command_line(const char* line, ProtocolCommand* out, ProtocolError* error);
bool format_command_line(const ProtocolCommand& command, char* output, size_t output_size);
const char* protocol_error_name(ProtocolError error);
