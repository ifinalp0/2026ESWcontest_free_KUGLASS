#pragma once

#include <cstdint>

enum class ControlProtocolError : uint8_t {
    OK = 0,
    EMPTY,
    BAD_JSON,
    DUPLICATE_FIELD,
    BAD_VERSION,
    BAD_TYPE,
    MISSING_VERSION,
    MISSING_SEQ,
    BAD_SEQ,
    MISSING_SOURCE_SESSION_ID,
    BAD_SOURCE_SESSION_ID,
    MISSING_TARGET_BOOT_ID,
    BAD_TARGET_BOOT_ID,
    MISSING_RESET_CHALLENGE,
    BAD_RESET_CHALLENGE,
    BAD_COMMAND,
};

struct ResetFaultCommand {
    uint32_t seq = 0;
    uint32_t source_session_id = 0;
    uint32_t target_boot_id = 0;
    uint32_t reset_challenge = 0;
};

// A reset is bound to both controller boots and to B's one-time challenge:
// {"v":1,"type":"control","seq":N,"source_session_id":A,
//  "target_boot_id":B,"reset_challenge":C,"command":"reset_fault"}
bool parse_reset_fault_line(const char* line,
                            ResetFaultCommand* output,
                            ControlProtocolError* error);
const char* control_protocol_error_name(ControlProtocolError error);
