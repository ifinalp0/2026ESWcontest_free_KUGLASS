#pragma once

#include "kuglass_b_config.h"

#include <cstdint>

enum class EstopInputQualification : uint8_t {
    HEALTHY = 0,
    QUALIFYING_LOW,
    QUALIFYING_HIGH,
    CONFIRMED_LOW,
    RECOVERED_HIGH,
};

// Qualification is deliberately separate from the electrical cut. The Logic
// Carrier gates CHx_ENABLE with raw EN_GLOBAL, and the GPIO ISR lowers every
// MCU ENABLE request immediately. This class decides only whether a raw event
// becomes a reset-required latch or may recover as a short glitch.
class EstopInputQualifier {
public:
    void reset() {
        pending_ = false;
        confirmed_ = false;
        consecutive_low_samples_ = 0;
        consecutive_high_samples_ = 0;
    }

    void note_falling_edge() {
        pending_ = true;
        confirmed_ = false;
        consecutive_low_samples_ = 0;
        consecutive_high_samples_ = 0;
    }

    EstopInputQualification sample(bool estop_n_high) {
        if (!pending_) return EstopInputQualification::HEALTHY;
        if (confirmed_) return EstopInputQualification::CONFIRMED_LOW;

        if (!estop_n_high) {
            consecutive_high_samples_ = 0;
            if (consecutive_low_samples_ < KUGLASS_ESTOP_CONFIRM_SAMPLES) {
                ++consecutive_low_samples_;
            }
            if (consecutive_low_samples_ >=
                KUGLASS_ESTOP_CONFIRM_SAMPLES) {
                confirmed_ = true;
                return EstopInputQualification::CONFIRMED_LOW;
            }
            return EstopInputQualification::QUALIFYING_LOW;
        }

        consecutive_low_samples_ = 0;
        if (consecutive_high_samples_ < KUGLASS_ESTOP_RELEASE_SAMPLES) {
            ++consecutive_high_samples_;
        }
        if (consecutive_high_samples_ >= KUGLASS_ESTOP_RELEASE_SAMPLES) {
            pending_ = false;
            return EstopInputQualification::RECOVERED_HIGH;
        }
        return EstopInputQualification::QUALIFYING_HIGH;
    }

    bool pending() const { return pending_; }
    bool confirmed() const { return confirmed_; }
    uint8_t consecutive_low_samples() const {
        return consecutive_low_samples_;
    }
    uint8_t consecutive_high_samples() const {
        return consecutive_high_samples_;
    }

private:
    bool pending_ = false;
    bool confirmed_ = false;
    uint8_t consecutive_low_samples_ = 0;
    uint8_t consecutive_high_samples_ = 0;
};
