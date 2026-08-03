#pragma once

#include "kuglass_b_config.h"

struct PowerStageChannelPins {
    int pwm_gpio;
    int direction_gpio;
    int enable_gpio;
    int fault_n_gpio;
};

struct PowerStagePinmap {
    int estop_n_gpio;
    PowerStageChannelPins channels[KUGLASS_CHANNEL_COUNT];
};

// Verify these direct ESP32_B-to-Power-Stage connections on the assembled board.
static constexpr PowerStagePinmap KUGLASS_POWER_STAGE_PINMAP = {
    3,
    {
        {4, 5, 6, 7},
        {8, 9, 10, 11},
        {12, 13, 14, 15},
        {16, 17, 18, 21},
    },
};
