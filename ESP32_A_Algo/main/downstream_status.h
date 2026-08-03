#pragma once

#include "kuglass_config.h"

#include <cstdint>

struct DownstreamChannelStatus {
    uint8_t channel_id = 0;
    float applied_mi = 0.0f;
    bool fault = false;
};

struct DownstreamStatus {
    uint32_t seq = 0;
    DownstreamChannelStatus channels[KUGLASS_TOTAL_CHANNELS];
};

enum class DownstreamStatusError : uint8_t {
    OK = 0,
    EMPTY,
    BAD_JSON,
    MISSING_VERSION,
    BAD_VERSION,
    MISSING_TYPE,
    BAD_TYPE,
    MISSING_CONTROLLER,
    BAD_CONTROLLER,
    MISSING_SEQ,
    MISSING_CHANNELS,
    BAD_CHANNEL,
    DUPLICATE_CHANNEL,
    INCOMPLETE_CHANNELS,
    DUPLICATE_FIELD,
};

// Allocation-free validator for the ESP32_B -> ESP32_A status contract:
// {"v":1,"type":"status","controller_id":"B","seq":N,
//  "ch":[{"id":0,"mi":0.5,"fault":false}, ... id3]}
bool parse_downstream_status_line(const char* line,
                                  DownstreamStatus* output,
                                  DownstreamStatusError* error);
const char* downstream_status_error_name(DownstreamStatusError error);
