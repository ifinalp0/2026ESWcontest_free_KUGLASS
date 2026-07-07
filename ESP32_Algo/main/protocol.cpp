#include "protocol.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char* skip_ws(const char* p) {
    while (p != nullptr && *p != '\0' && std::isspace(static_cast<unsigned char>(*p))) {
        ++p;
    }
    return p;
}

const char* find_key_value(const char* line, const char* key) {
    char pattern[32];
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = std::strstr(line, pattern);
    if (key_pos == nullptr) {
        return nullptr;
    }
    const char* colon = std::strchr(key_pos + std::strlen(pattern), ':');
    return colon == nullptr ? nullptr : skip_ws(colon + 1);
}

bool parse_u32_key(const char* line, const char* key, uint32_t* out) {
    const char* p = find_key_value(line, key);
    if (p == nullptr) {
        return false;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parse_bool_or_int(const char*& p, bool* out) {
    p = skip_ws(p);
    if (std::strncmp(p, "true", 4) == 0) {
        *out = true;
        p += 4;
        return true;
    }
    if (std::strncmp(p, "false", 5) == 0) {
        *out = false;
        p += 5;
        return true;
    }
    char* end = nullptr;
    long value = std::strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = value != 0;
    p = end;
    return true;
}

bool parse_channel_array(const char* line, ProtocolCommand* out, ProtocolError* error) {
    const char* p = find_key_value(line, "ch");
    if (p == nullptr || *p != '[') {
        *error = ProtocolError::MISSING_CHANNELS;
        return false;
    }
    ++p;
    out->channel_count = 0;
    while (*p != '\0') {
        p = skip_ws(p);
        if (*p == ']') {
            *error = ProtocolError::OK;
            return true;
        }
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p != '[') {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        if (out->channel_count >= KUGLASS_MAX_COMMAND_CHANNELS) {
            *error = ProtocolError::TOO_MANY_CHANNELS;
            return false;
        }
        ++p;
        char* end = nullptr;
        unsigned long channel_id = std::strtoul(skip_ws(p), &end, 10);
        if (end == p || channel_id > 255) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        p = skip_ws(end);
        if (*p != ',') {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        ++p;
        float mi = std::strtof(skip_ws(p), &end);
        if (end == p) {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        p = skip_ws(end);
        bool enable = true;
        if (*p == ',') {
            ++p;
            if (!parse_bool_or_int(p, &enable)) {
                *error = ProtocolError::BAD_CHANNEL_ARRAY;
                return false;
            }
            p = skip_ws(p);
        }
        if (*p != ']') {
            *error = ProtocolError::BAD_CHANNEL_ARRAY;
            return false;
        }
        ++p;
        if (mi < 0.0f) {
            mi = 0.0f;
        }
        if (mi > 1.0f) {
            mi = 1.0f;
        }
        out->channels[out->channel_count++] = ProtocolChannelCommand{
            .channel_id = static_cast<uint8_t>(channel_id),
            .mi = mi,
            .enable = enable,
        };
    }
    *error = ProtocolError::BAD_CHANNEL_ARRAY;
    return false;
}

}  // namespace

bool parse_command_line(const char* line, ProtocolCommand* out, ProtocolError* error) {
    if (out == nullptr || error == nullptr) {
        return false;
    }
    *out = ProtocolCommand{};
    *error = ProtocolError::OK;
    if (line == nullptr || *skip_ws(line) == '\0') {
        *error = ProtocolError::EMPTY;
        return false;
    }
    if (!parse_u32_key(line, "seq", &out->seq)) {
        *error = ProtocolError::MISSING_SEQ;
        return false;
    }
    if (!parse_u32_key(line, "ttl_ms", &out->ttl_ms)) {
        *error = ProtocolError::MISSING_TTL;
        return false;
    }
    if (out->ttl_ms < 50) {
        out->ttl_ms = 50;
    }
    if (out->ttl_ms > 1000) {
        out->ttl_ms = 1000;
    }
    return parse_channel_array(line, out, error);
}

const char* protocol_error_name(ProtocolError error) {
    switch (error) {
        case ProtocolError::OK:
            return "OK";
        case ProtocolError::EMPTY:
            return "EMPTY";
        case ProtocolError::MISSING_SEQ:
            return "MISSING_SEQ";
        case ProtocolError::MISSING_TTL:
            return "MISSING_TTL";
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
