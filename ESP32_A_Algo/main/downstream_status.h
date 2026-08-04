#pragma once

#include "kuglass_config.h"

#include <cstdint>

struct DownstreamChannelStatus {
    uint8_t channel_id = 0;
    float applied_mi = 0.0f;
    bool fault = false;
};

enum class DownstreamFaultCode : uint8_t {
    NONE = 0,
    COMM_TIMEOUT,
    INVALID_COMMAND,
    POWER_STAGE_FAULT,
    ESTOP,
};

struct DownstreamAdcStatus {
    bool initialized = false;
    bool current_calibrated = false;
    bool temperature_calibrated = false;
    uint8_t raw_valid_mask = 0;
    uint8_t mv_valid_mask = 0;
    uint16_t current_raw[KUGLASS_TOTAL_CHANNELS] = {};
    uint16_t temperature_raw[KUGLASS_TOTAL_CHANNELS] = {};
    uint16_t current_mv[KUGLASS_TOTAL_CHANNELS] = {};
    uint16_t temperature_mv[KUGLASS_TOTAL_CHANNELS] = {};
};

enum class DownstreamControlResultError : uint8_t {
    NONE = 0,
    RESET_UNSAFE,
    TARGET_BOOT_MISMATCH,
    CHALLENGE_MISMATCH,
};

struct DownstreamControlResult {
    uint32_t seq = 0;
    uint32_t source_session_id = 0;
    bool ok = false;
    DownstreamControlResultError error = DownstreamControlResultError::NONE;
};

struct DownstreamStatus {
    uint32_t seq = 0;
    uint32_t boot_id = 0;
    uint32_t reset_challenge = 0;
    bool estop = false;
    DownstreamFaultCode fault_code = DownstreamFaultCode::NONE;
    bool has_diagnostic = false;
    char diagnostic[40] = {};
    bool has_adc = false;
    DownstreamAdcStatus adc;
    bool has_control_result = false;
    DownstreamControlResult control_result;
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
    MISSING_BOOT_ID,
    BAD_BOOT_ID,
    MISSING_RESET_CHALLENGE,
    BAD_RESET_CHALLENGE,
    MISSING_ESTOP,
    BAD_ESTOP,
    MISSING_FAULT_CODE,
    BAD_FAULT_CODE,
    BAD_DIAGNOSTIC,
    BAD_ADC,
    BAD_CONTROL_RESULT,
    MISSING_CHANNELS,
    BAD_CHANNEL,
    DUPLICATE_CHANNEL,
    INCOMPLETE_CHANNELS,
    DUPLICATE_FIELD,
};

// Allocation-free validator for the ESP32_B -> ESP32_A status contract.
// boot_id/reset_challenge are mandatory and nonzero. adc, diagnostic and
// control_result are optional typed extensions; unknown top-level fields are
// skipped for forward compatibility.
bool parse_downstream_status_line(const char* line,
                                  DownstreamStatus* output,
                                  DownstreamStatusError* error);
const char* downstream_status_error_name(DownstreamStatusError error);
const char* downstream_fault_code_name(DownstreamFaultCode code);
const char* downstream_control_result_error_name(
    DownstreamControlResultError error);
