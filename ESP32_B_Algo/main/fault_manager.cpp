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
    if (code_ == FaultCode::COMM_TIMEOUT ||
        code_ == FaultCode::INVALID_COMMAND) {
        faulted_ = false;
        code_ = FaultCode::NONE;
    }
}

void FaultManager::reject_command() {
    command_received_ = false;
    latch(FaultCode::INVALID_COMMAND);
}

void FaultManager::update(uint32_t now_ms, bool estop_ok) {
    if (!estop_ok) {
        latch(FaultCode::ESTOP);
    } else if (!command_received_ ||
               static_cast<uint32_t>(now_ms - last_command_ms_) > ttl_ms_) {
        latch(FaultCode::COMM_TIMEOUT);
    }
}

bool FaultManager::clear_if_safe(bool estop_ok) {
    if (!estop_ok) return false;
    faulted_ = false;
    code_ = FaultCode::NONE;
    command_received_ = false;
    return true;
}

const char* FaultManager::code_name() const {
    switch (code_) {
        case FaultCode::NONE: return "NONE";
        case FaultCode::COMM_TIMEOUT: return "COMM_TIMEOUT";
        case FaultCode::INVALID_COMMAND: return "INVALID_COMMAND";
        case FaultCode::ESTOP: return "ESTOP";
        default: return "UNKNOWN";
    }
}

void FaultManager::latch(FaultCode code) {
    const auto priority = [](FaultCode value) {
        switch (value) {
            case FaultCode::ESTOP: return 4;
            case FaultCode::INVALID_COMMAND: return 2;
            case FaultCode::COMM_TIMEOUT: return 1;
            case FaultCode::NONE: return 0;
            default: return 0;
        }
    };
    faulted_ = true;
    if (priority(code) > priority(code_)) code_ = code;
}
