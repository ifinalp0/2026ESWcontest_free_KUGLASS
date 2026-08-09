#pragma once

#include "kuglass_b_config.h"

#include <cstdint>

enum class FaultInputQualification : uint8_t {
    HEALTHY = 0,
    QUALIFYING_LOW,
    QUALIFYING_HIGH,
    CONFIRMED_LOW,
    RECOVERED_HIGH,
};

// Software qualification is deliberately separate from the electrical cut.
// FAULT_N still lowers RUN_OK in hardware immediately, and the GPIO ISR lowers
// the MCU ENABLE request immediately. This class decides whether a LOW
// persisted long enough, or recurred soon enough, to require an explicit
// fault reset.
class FaultInputQualifier {
public:
    void reset() {
        pending_ = false;
        consecutive_low_samples_ = 0;
        consecutive_high_samples_ = 0;
        repeat_event_count_ = 0;
        repeat_window_started_ms_ = 0;
    }

    // Returns true when the input has produced enough distinct falling edges
    // inside the bounded window to be treated as an intermittent real fault.
    // This closes the hiccup path where cutting ENABLE clears FAULT_N before
    // the persistent-LOW qualifier can ever reach ten samples.
    bool note_falling_edge(uint32_t now_ms) {
        pending_ = true;
        consecutive_low_samples_ = 0;
        consecutive_high_samples_ = 0;
        if (repeat_event_count_ == 0U ||
            static_cast<uint32_t>(now_ms - repeat_window_started_ms_) >
                KUGLASS_FAULT_REPEAT_WINDOW_MS) {
            repeat_event_count_ = 1U;
            repeat_window_started_ms_ = now_ms;
        } else if (repeat_event_count_ < UINT8_MAX) {
            ++repeat_event_count_;
        }
        return repeat_event_count_ >= KUGLASS_FAULT_REPEAT_EVENT_LIMIT;
    }

    FaultInputQualification sample(bool fault_n_high) {
        // Also handles a LOW that was already present before the GPIO falling
        // edge interrupt was configured at boot.
        if (!pending_ && !fault_n_high) pending_ = true;
        if (!pending_) return FaultInputQualification::HEALTHY;

        if (fault_n_high) {
            consecutive_low_samples_ = 0;
            if (consecutive_high_samples_ < KUGLASS_FAULT_RELEASE_SAMPLES) {
                ++consecutive_high_samples_;
            }
            if (consecutive_high_samples_ >= KUGLASS_FAULT_RELEASE_SAMPLES) {
                pending_ = false;
                return FaultInputQualification::RECOVERED_HIGH;
            }
            return FaultInputQualification::QUALIFYING_HIGH;
        }
        consecutive_high_samples_ = 0;
        if (consecutive_low_samples_ < KUGLASS_FAULT_CONFIRM_SAMPLES) {
            ++consecutive_low_samples_;
        }
        return consecutive_low_samples_ >= KUGLASS_FAULT_CONFIRM_SAMPLES
            ? FaultInputQualification::CONFIRMED_LOW
            : FaultInputQualification::QUALIFYING_LOW;
    }

    uint8_t consecutive_low_samples() const {
        return consecutive_low_samples_;
    }
    uint8_t consecutive_high_samples() const {
        return consecutive_high_samples_;
    }
    uint8_t repeat_event_count() const { return repeat_event_count_; }
    bool pending() const { return pending_; }

private:
    bool pending_ = false;
    uint8_t consecutive_low_samples_ = 0;
    uint8_t consecutive_high_samples_ = 0;
    uint8_t repeat_event_count_ = 0;
    uint32_t repeat_window_started_ms_ = 0;
};
