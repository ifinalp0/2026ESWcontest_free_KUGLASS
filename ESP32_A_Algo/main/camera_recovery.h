#pragma once

#include <cstdint>

// Platform-independent camera recovery state. Keeping retry/backoff decisions
// outside the ESP-IDF adapter makes the failure path deterministic and host
// testable.
class CameraRecoveryState {
public:
    CameraRecoveryState(uint32_t initial_retry_ms,
                        uint32_t maximum_retry_ms,
                        uint8_t failures_before_restart);

    bool start_due(uint32_t now_ms) const;
    void note_start_result(bool success, uint32_t now_ms);

    // Returns true when the caller must stop the camera service.
    bool note_capture_result(bool success, uint32_t now_ms);

    bool running() const { return running_; }
    uint32_t next_start_ms() const { return next_start_ms_; }
    uint32_t retry_delay_ms() const { return retry_delay_ms_; }
    uint8_t consecutive_capture_failures() const {
        return consecutive_capture_failures_;
    }

private:
    void schedule_retry(uint32_t now_ms);

    uint32_t initial_retry_ms_;
    uint32_t maximum_retry_ms_;
    uint8_t failures_before_restart_;
    bool running_ = false;
    uint8_t consecutive_capture_failures_ = 0;
    uint32_t next_start_ms_ = 0;
    uint32_t retry_delay_ms_;
};
