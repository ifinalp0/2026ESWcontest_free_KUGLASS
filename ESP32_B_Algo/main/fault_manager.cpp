#include "fault_manager.h"

#include "kuglass_b_config.h"

void FaultManager::begin() {
    faulted_ = false;
    code_ = FaultCode::NONE;
    command_received_ = false;
    last_command_ms_ = 0;
    ttl_ms_ = KUGLASS_DEFAULT_TTL_MS;
}

void FaultManager::note_command(uint32_t now_ms, uint32_t ttl_ms) {
    command_received_ = true;
    last_command_ms_ = now_ms;
    ttl_ms_ = ttl_ms;
    if (code_ == FaultCode::COMM_TIMEOUT) {
        faulted_ = false;
        code_ = FaultCode::NONE;
    }
}

void FaultManager::update(uint32_t now_ms, bool estop_ok, bool power_stages_ok) {
    if (!estop_ok) {
        latch(FaultCode::ESTOP);
    } else if (!power_stages_ok) {
        latch(FaultCode::POWER_STAGE_FAULT);
    } else if (!command_received_ ||
               static_cast<uint32_t>(now_ms - last_command_ms_) > ttl_ms_) {
        latch(FaultCode::COMM_TIMEOUT);
    }
}

bool FaultManager::clear_if_safe(bool estop_ok, bool power_stages_ok) {
    if (!estop_ok || !power_stages_ok) return false;
    faulted_ = false;
    code_ = FaultCode::NONE;
    command_received_ = false;
    return true;
}

const char* FaultManager::code_name() const {
    switch (code_) {
        case FaultCode::NONE: return "NONE";
        case FaultCode::COMM_TIMEOUT: return "COMM_TIMEOUT";
        case FaultCode::ESTOP: return "ESTOP";
        case FaultCode::POWER_STAGE_FAULT: return "POWER_STAGE_FAULT";
        default: return "UNKNOWN";
    }
}

void FaultManager::latch(FaultCode code) {
    faulted_ = true;
    if (code_ == FaultCode::NONE || code != FaultCode::COMM_TIMEOUT) code_ = code;
}
