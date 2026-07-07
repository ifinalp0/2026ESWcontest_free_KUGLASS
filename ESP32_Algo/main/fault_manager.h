#pragma once

#include <cstdint>

enum class FaultCode : uint8_t {
    NONE = 0,
    COMM_TIMEOUT,
    ESTOP,
    LOCAL_FAULT_PIN,
    OVER_CURRENT,
    OVER_VOLTAGE,
    OVER_TEMP,
};

class FaultManager {
public:
    void begin();
    void note_command(uint32_t now_ms, uint32_t ttl_ms);
    void update(uint32_t now_ms, bool estop_ok, bool local_fault_ok);
    bool faulted() const { return faulted_; }
    FaultCode code() const { return code_; }
    const char* code_name() const;
    uint32_t ttl_ms() const { return ttl_ms_; }

private:
    void latch(FaultCode code);

    bool faulted_ = false;
    FaultCode code_ = FaultCode::NONE;
    bool ever_received_command_ = false;
    uint32_t last_command_ms_ = 0;
    uint32_t ttl_ms_ = 200;
};

