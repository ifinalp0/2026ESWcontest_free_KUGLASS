#pragma once

#include <cstdint>

struct ChannelPins {
    int pwm_gpio;
    int dir_gpio;
    int en_gpio;
    int fault_n_gpio;
    int adc_v_channel;
    int adc_i_channel;
    int adc_temp_channel;
};

struct CarrierPinmap {
    int estop_n_gpio;
    ChannelPins channel[4];
};

static constexpr CarrierPinmap KUGLASS_PINMAP = {
    .estop_n_gpio = 3,
    .channel = {
        {.pwm_gpio = 4, .dir_gpio = 5, .en_gpio = 6, .fault_n_gpio = 7, .adc_v_channel = 0, .adc_i_channel = 1, .adc_temp_channel = 2},
        {.pwm_gpio = 8, .dir_gpio = 9, .en_gpio = 10, .fault_n_gpio = 11, .adc_v_channel = 3, .adc_i_channel = 4, .adc_temp_channel = 5},
        {.pwm_gpio = 12, .dir_gpio = 13, .en_gpio = 14, .fault_n_gpio = 15, .adc_v_channel = 6, .adc_i_channel = 7, .adc_temp_channel = 8},
        {.pwm_gpio = 16, .dir_gpio = 17, .en_gpio = 18, .fault_n_gpio = 21, .adc_v_channel = 9, .adc_i_channel = 10, .adc_temp_channel = 11},
    },
};

