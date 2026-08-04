#include "spwm_generator.h"

#include "kuglass_b_config.h"

#include <cmath>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"

namespace {
constexpr uint32_t kPwmResolutionHz = 10000000;
constexpr uint32_t kPwmPeriodTicks =
    static_cast<uint32_t>(kPwmResolutionHz / KUGLASS_CARRIER_HZ);

struct PwmHandle {
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_oper_handle_t operator_handle = nullptr;
    mcpwm_cmpr_handle_t comparator = nullptr;
    mcpwm_gen_handle_t generator = nullptr;
    int direction_gpio = -1;
    int enable_gpio = -1;
};

PwmHandle g_pwm[KUGLASS_CHANNEL_COUNT];
}
#endif

void SpwmGenerator::begin(const PowerStagePinmap& pinmap) {
#ifdef ESP_PLATFORM
    gpio_config_t output_config = {};
    output_config.mode = GPIO_MODE_OUTPUT;
    for (const PowerStageChannelPins& pins : pinmap.channels) {
        output_config.pin_bit_mask |=
            (1ULL << pins.direction_gpio) | (1ULL << pins.enable_gpio);
        // Preload the output latch before switching the pads to output mode.
        gpio_set_level(static_cast<gpio_num_t>(pins.enable_gpio), 0);
        gpio_set_level(static_cast<gpio_num_t>(pins.direction_gpio), 0);
    }
    ESP_ERROR_CHECK(gpio_config(&output_config));
    for (const PowerStageChannelPins& pins : pinmap.channels) {
        gpio_set_level(static_cast<gpio_num_t>(pins.enable_gpio), 0);
        gpio_set_level(static_cast<gpio_num_t>(pins.direction_gpio), 0);
    }

    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const PowerStageChannelPins& pins = pinmap.channels[i];
        const int group_id = i < 3U ? 0 : 1;
        mcpwm_timer_config_t timer_config = {};
        timer_config.group_id = group_id;
        timer_config.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
        timer_config.resolution_hz = kPwmResolutionHz;
        timer_config.period_ticks = kPwmPeriodTicks;
        timer_config.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
        ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &g_pwm[i].timer));

        mcpwm_operator_config_t operator_config = {};
        operator_config.group_id = group_id;
        ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &g_pwm[i].operator_handle));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(
            g_pwm[i].operator_handle, g_pwm[i].timer));

        mcpwm_comparator_config_t comparator_config = {};
        comparator_config.flags.update_cmp_on_tez = true;
        ESP_ERROR_CHECK(mcpwm_new_comparator(
            g_pwm[i].operator_handle, &comparator_config, &g_pwm[i].comparator));

        mcpwm_generator_config_t generator_config = {};
        generator_config.gen_gpio_num = pins.pwm_gpio;
        ESP_ERROR_CHECK(mcpwm_new_generator(
            g_pwm[i].operator_handle, &generator_config, &g_pwm[i].generator));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
            g_pwm[i].generator,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                         MCPWM_TIMER_EVENT_EMPTY,
                                         MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
            g_pwm[i].generator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                           g_pwm[i].comparator,
                                           MCPWM_GEN_ACTION_LOW)));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(g_pwm[i].comparator, 0));
        ESP_ERROR_CHECK(mcpwm_timer_enable(g_pwm[i].timer));
        ESP_ERROR_CHECK(mcpwm_timer_start_stop(
            g_pwm[i].timer, MCPWM_TIMER_START_NO_STOP));
        g_pwm[i].direction_gpio = pins.direction_gpio;
        g_pwm[i].enable_gpio = pins.enable_gpio;
    }
#else
    (void)pinmap;
#endif
}

void SpwmGenerator::tick(ChannelManager& channels, float dt_s, bool global_enable) {
    constexpr float kPi = 3.14159265358979323846f;
    phase_rad_ += 2.0f * kPi * KUGLASS_FUNDAMENTAL_HZ * dt_s;
    if (phase_rad_ > 2.0f * kPi) phase_rad_ = std::fmod(phase_rad_, 2.0f * kPi);
    const float sine = std::sin(phase_rad_);
    const bool positive = sine >= 0.0f;
    const float magnitude = std::fabs(sine);

    for (size_t i = 0; i < channels.count(); ++i) {
        ChannelRuntime* item = channels.channel(i);
        if (item == nullptr) continue;
        const bool enable = global_enable && item->enable && !item->faulted &&
                            item->applied_mi > 0.001f;
        set_channel_output(i, enable ? item->applied_mi * magnitude : 0.0f,
                           positive, enable);
    }
}

void SpwmGenerator::set_channel_output(size_t index,
                                       float duty,
                                       bool positive,
                                       bool enable) {
#ifdef ESP_PLATFORM
    if (index >= KUGLASS_CHANNEL_COUNT) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(
        g_pwm[index].comparator,
        static_cast<uint32_t>(duty * kPwmPeriodTicks)));
    gpio_set_level(static_cast<gpio_num_t>(g_pwm[index].direction_gpio),
                   positive ? 1 : 0);
    gpio_set_level(static_cast<gpio_num_t>(g_pwm[index].enable_gpio),
                   enable ? 1 : 0);
#else
    (void)index;
    (void)duty;
    (void)positive;
    (void)enable;
#endif
}
