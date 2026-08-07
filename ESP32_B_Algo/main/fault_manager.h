#pragma once

#include <cstdint>

enum class FaultCode : uint8_t {
    NONE = 0,
    COMM_TIMEOUT,
    INVALID_COMMAND,
    ESTOP,
};

class FaultManager {
public:
    void begin();
    void note_command(uint32_t now_ms, uint32_t ttl_ms);
    void reject_command();
    void update(uint32_t now_ms, bool estop_ok);
    bool clear_if_safe(bool estop_ok);
    bool faulted() const { return faulted_; }
    FaultCode code() const { return code_; }
    const char* code_name() const;

private:
    void latch(FaultCode code);

    bool faulted_ = false;
    FaultCode code_ = FaultCode::NONE;
    bool command_received_ = false;
    uint32_t last_command_ms_ = 0;
    uint32_t ttl_ms_ = 250;
};
