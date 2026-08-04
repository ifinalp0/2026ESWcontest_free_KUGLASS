#pragma once

#include "kuglass_b_config.h"

#include <cstddef>

struct PowerStageChannelPins {
    int pwm_gpio;
    int direction_gpio;
    int enable_gpio;
    int fault_n_gpio;
    int current_adc_gpio;
    int temperature_adc_gpio;
};

struct PowerStagePinmap {
    int estop_n_gpio;
    PowerStageChannelPins channels[KUGLASS_CHANNEL_COUNT];
};

// Logic Carrier U3 pin ownership from hardware/Logic carrier.pdf.
static constexpr PowerStagePinmap KUGLASS_POWER_STAGE_PINMAP = {
    19,
    {
        {10, 11, 12, 13, 1, 2},
        {14, 15, 16, 17, 4, 5},
        {18, 21, 38, 39, 6, 7},
        {40, 41, 42, 47, 8, 3},
    },
};

constexpr bool kuglass_power_stage_owns_gpio(int gpio) {
    if (gpio == KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio) return true;
    for (const PowerStageChannelPins& channel :
         KUGLASS_POWER_STAGE_PINMAP.channels) {
        if (gpio == channel.pwm_gpio || gpio == channel.direction_gpio ||
            gpio == channel.enable_gpio || gpio == channel.fault_n_gpio ||
            gpio == channel.current_adc_gpio ||
            gpio == channel.temperature_adc_gpio) {
            return true;
        }
    }
    return false;
}

constexpr bool kuglass_power_stage_pinmap_is_unique() {
    int pins[1 + KUGLASS_CHANNEL_COUNT * 6] = {};
    std::size_t count = 0;
    pins[count++] = KUGLASS_POWER_STAGE_PINMAP.estop_n_gpio;
    for (const PowerStageChannelPins& channel :
         KUGLASS_POWER_STAGE_PINMAP.channels) {
        pins[count++] = channel.pwm_gpio;
        pins[count++] = channel.direction_gpio;
        pins[count++] = channel.enable_gpio;
        pins[count++] = channel.fault_n_gpio;
        pins[count++] = channel.current_adc_gpio;
        pins[count++] = channel.temperature_adc_gpio;
    }
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (pins[i] == pins[j]) return false;
        }
    }
    return true;
}

static_assert(kuglass_power_stage_pinmap_is_unique(),
              "Logic Carrier GPIO ownership must be unique.");
static_assert(KUGLASS_A_UART_TX_GPIO != KUGLASS_A_UART_RX_GPIO,
              "ESP32_A link UART TX and RX GPIO must differ.");
static_assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_TX_GPIO),
              "ESP32_A link UART TX conflicts with Logic Carrier GPIO.");
static_assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_RX_GPIO),
              "ESP32_A link UART RX conflicts with Logic Carrier GPIO.");
