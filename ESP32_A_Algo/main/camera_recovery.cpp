#include "camera_recovery.h"

namespace {

bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace

CameraRecoveryState::CameraRecoveryState(uint32_t initial_retry_ms,
                                         uint32_t maximum_retry_ms,
                                         uint8_t failures_before_restart)
    : initial_retry_ms_(initial_retry_ms == 0U ? 1U : initial_retry_ms),
      maximum_retry_ms_(maximum_retry_ms < initial_retry_ms_
                            ? initial_retry_ms_
                            : maximum_retry_ms),
      failures_before_restart_(failures_before_restart == 0U
                                   ? 1U
                                   : failures_before_restart),
      retry_delay_ms_(initial_retry_ms_) {}

bool CameraRecoveryState::start_due(uint32_t now_ms) const {
    return !running_ && time_reached(now_ms, next_start_ms_);
}

void CameraRecoveryState::schedule_retry(uint32_t now_ms) {
    next_start_ms_ = now_ms + retry_delay_ms_;
    if (retry_delay_ms_ < maximum_retry_ms_) {
        const uint64_t doubled = static_cast<uint64_t>(retry_delay_ms_) * 2ULL;
        retry_delay_ms_ = doubled > maximum_retry_ms_
                              ? maximum_retry_ms_
                              : static_cast<uint32_t>(doubled);
    }
}

void CameraRecoveryState::note_start_result(bool success, uint32_t now_ms) {
    running_ = success;
    consecutive_capture_failures_ = 0;
    if (success) {
        retry_delay_ms_ = initial_retry_ms_;
    } else {
        schedule_retry(now_ms);
    }
}

bool CameraRecoveryState::note_capture_result(bool success, uint32_t now_ms) {
    if (!running_) return false;
    if (success) {
        consecutive_capture_failures_ = 0;
        retry_delay_ms_ = initial_retry_ms_;
        return false;
    }

    if (consecutive_capture_failures_ < UINT8_MAX) {
        ++consecutive_capture_failures_;
    }
    if (consecutive_capture_failures_ < failures_before_restart_) return false;

    running_ = false;
    consecutive_capture_failures_ = 0;
    schedule_retry(now_ms);
    return true;
}
