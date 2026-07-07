#include "spwm_generator.h"

#include "kuglass_config.h"

#include <cmath>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"

namespace {
constexpr uint32_t PWM_RESOLUTION_HZ = 10000000;
constexpr uint32_t PWM_PERIOD_TICKS = static_cast<uint32_t>(PWM_RESOLUTION_HZ / KUGLASS_CARRIER_HZ);
const char* TAG = "kuglass_spwm";

struct PwmHandle {
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_oper_handle_t oper = nullptr;
    mcpwm_cmpr_handle_t comparator = nullptr;
    mcpwm_gen_handle_t generator = nullptr;
    int dir_gpio = -1;
    int en_gpio = -1;
};

PwmHandle g_pwm[4];
CarrierPinmap g_pinmap = KUGLASS_PINMAP;
}  // namespace
#endif

void SpwmGenerator::begin(const CarrierPinmap& pinmap) {
#ifdef ESP_PLATFORM
    g_pinmap = pinmap;
    for (size_t i = 0; i < KUGLASS_LOCAL_CHANNELS; ++i) {
        const ChannelPins& pins = pinmap.channel[i];
        gpio_config_t gpio_cfg = {};
        gpio_cfg.mode = GPIO_MODE_OUTPUT;
        gpio_cfg.pin_bit_mask = (1ULL << pins.dir_gpio) | (1ULL << pins.en_gpio);
        gpio_config(&gpio_cfg);
        gpio_set_level(static_cast<gpio_num_t>(pins.en_gpio), 0);
        gpio_set_level(static_cast<gpio_num_t>(pins.dir_gpio), 0);

        const int group_id = i < 3 ? 0 : 1;
        mcpwm_timer_config_t timer_config = {};
        timer_config.group_id = group_id;
        timer_config.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
        timer_config.resolution_hz = PWM_RESOLUTION_HZ;
        timer_config.period_ticks = PWM_PERIOD_TICKS;
        timer_config.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
        ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &g_pwm[i].timer));

        mcpwm_operator_config_t operator_config = {};
        operator_config.group_id = group_id;
        ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &g_pwm[i].oper));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(g_pwm[i].oper, g_pwm[i].timer));

        mcpwm_comparator_config_t comparator_config = {};
        comparator_config.flags.update_cmp_on_tez = true;
        ESP_ERROR_CHECK(mcpwm_new_comparator(g_pwm[i].oper, &comparator_config, &g_pwm[i].comparator));

        mcpwm_generator_config_t generator_config = {};
        generator_config.gen_gpio_num = pins.pwm_gpio;
        ESP_ERROR_CHECK(mcpwm_new_generator(g_pwm[i].oper, &generator_config, &g_pwm[i].generator));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
            g_pwm[i].generator,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
            g_pwm[i].generator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_pwm[i].comparator, MCPWM_GEN_ACTION_LOW)));
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(g_pwm[i].comparator, 0));
        ESP_ERROR_CHECK(mcpwm_timer_enable(g_pwm[i].timer));
        ESP_ERROR_CHECK(mcpwm_timer_start_stop(g_pwm[i].timer, MCPWM_TIMER_START_NO_STOP));
        g_pwm[i].dir_gpio = pins.dir_gpio;
        g_pwm[i].en_gpio = pins.en_gpio;
    }
    ESP_LOGI(TAG, "MCPWM SPWM backend initialized");
#else
    (void)pinmap;
#endif
}

void SpwmGenerator::tick(ChannelManager& channels, float dt_s, bool global_enable) {
    phase_rad_ += 2.0f * static_cast<float>(M_PI) * KUGLASS_FUNDAMENTAL_HZ * dt_s;
    if (phase_rad_ > 2.0f * static_cast<float>(M_PI)) {
        phase_rad_ = std::fmod(phase_rad_, 2.0f * static_cast<float>(M_PI));
    }
    const float sine = std::sin(phase_rad_);
    const bool positive = sine >= 0.0f;
    const float mag = std::fabs(sine);

    for (size_t i = 0; i < channels.count(); ++i) {
        ChannelRuntime* channel = channels.local(i);
        if (channel == nullptr) {
            continue;
        }
        const bool enable = global_enable && channel->enable && !channel->faulted && channel->applied_mi > 0.001f;
        const float duty = enable ? channel->applied_mi * mag : 0.0f;
        set_channel_output(i, duty, positive, enable);
    }
}

void SpwmGenerator::set_channel_output(size_t local_index, float duty, bool polarity_positive, bool enable) {
#ifdef ESP_PLATFORM
    if (local_index >= KUGLASS_LOCAL_CHANNELS) {
        return;
    }
    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    }
    const uint32_t compare_ticks = static_cast<uint32_t>(duty * PWM_PERIOD_TICKS);
    mcpwm_comparator_set_compare_value(g_pwm[local_index].comparator, compare_ticks);
    gpio_set_level(static_cast<gpio_num_t>(g_pwm[local_index].dir_gpio), polarity_positive ? 1 : 0);
    gpio_set_level(static_cast<gpio_num_t>(g_pwm[local_index].en_gpio), enable ? 1 : 0);
#else
    (void)local_index;
    (void)duty;
    (void)polarity_positive;
    (void)enable;
#endif
}

