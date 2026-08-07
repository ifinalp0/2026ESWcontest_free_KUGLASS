#pragma once

#include "kuglass_b_config.h"

#include <cstdint>

enum class FaultInputQualification : uint8_t {
    HEALTHY = 0,
    QUALIFYING_LOW,
    CONFIRMED_LOW,
};

// Software qualification is deliberately separate from the electrical cut.
// FAULT_N still lowers RUN_OK in hardware immediately, and the GPIO ISR lowers
// the MCU ENABLE request immediately. This class only decides whether the LOW
// persisted long enough to require an explicit fault reset.
class FaultInputQualifier {
public:
    void reset() { consecutive_low_samples_ = 0; }

    FaultInputQualification sample(bool fault_n_high) {
        if (fault_n_high) {
            reset();
            return FaultInputQualification::HEALTHY;
        }
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

private:
    uint8_t consecutive_low_samples_ = 0;
};
