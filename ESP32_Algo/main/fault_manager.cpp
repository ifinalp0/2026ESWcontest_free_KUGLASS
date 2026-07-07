#include "fault_manager.h"

#include "kuglass_config.h"

void FaultManager::begin() {
    faulted_ = false;
    code_ = FaultCode::NONE;
    ever_received_command_ = false;
    last_command_ms_ = 0;
    ttl_ms_ = KUGLASS_DEFAULT_TTL_MS;
}

void FaultManager::note_command(uint32_t now_ms, uint32_t ttl_ms) {
    ever_received_command_ = true;
    last_command_ms_ = now_ms;
    ttl_ms_ = ttl_ms;
    if (faulted_ && code_ == FaultCode::COMM_TIMEOUT) {
        faulted_ = false;
        code_ = FaultCode::NONE;
    }
}

void FaultManager::update(uint32_t now_ms, bool estop_ok, bool local_fault_ok) {
    if (!estop_ok) {
        latch(FaultCode::ESTOP);
        return;
    }
    if (!local_fault_ok) {
        latch(FaultCode::LOCAL_FAULT_PIN);
        return;
    }
    if (ever_received_command_ && now_ms - last_command_ms_ > ttl_ms_) {
        latch(FaultCode::COMM_TIMEOUT);
    }
}

const char* FaultManager::code_name() const {
    switch (code_) {
        case FaultCode::NONE:
            return "NONE";
        case FaultCode::COMM_TIMEOUT:
            return "COMM_TIMEOUT";
        case FaultCode::ESTOP:
            return "ESTOP";
        case FaultCode::LOCAL_FAULT_PIN:
            return "LOCAL_FAULT_PIN";
        case FaultCode::OVER_CURRENT:
            return "OVER_CURRENT";
        case FaultCode::OVER_VOLTAGE:
            return "OVER_VOLTAGE";
        case FaultCode::OVER_TEMP:
            return "OVER_TEMP";
        default:
            return "UNKNOWN";
    }
}

void FaultManager::latch(FaultCode code) {
    faulted_ = true;
    if (code_ == FaultCode::NONE || code != FaultCode::COMM_TIMEOUT) {
        code_ = code;
    }
}

