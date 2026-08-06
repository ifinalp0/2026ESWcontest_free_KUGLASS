#include "spwm_generator.h"

#include "kuglass_b_config.h"

#include <cmath>

namespace {
constexpr uint32_t kPwmResolutionHz = 10000000;
constexpr uint32_t kPwmPeriodTicks =
    static_cast<uint32_t>(kPwmResolutionHz / KUGLASS_CARRIER_HZ);

uint32_t duty_to_compare_ticks(float duty) {
    if (!std::isfinite(duty) || duty <= 0.0f) return 0U;
    return static_cast<uint32_t>(
        duty * static_cast<float>(kPwmPeriodTicks));
}
}

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"

namespace {
struct PwmHandle {
    mcpwm_timer_handle_t timer = nullptr;
    mcpwm_oper_handle_t operator_handle = nullptr;
    mcpwm_cmpr_handle_t comparator = nullptr;
    mcpwm_gen_handle_t generator = nullptr;
    int direction_gpio = -1;
    int enable_gpio = -1;
};

PwmHandle g_pwm[KUGLASS_CHANNEL_COUNT];
}  // namespace
#endif

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

float clamp_duty(float duty) {
    if (!std::isfinite(duty) || duty <= 0.0f) return 0.0f;
    if (duty > KUGLASS_MAX_MODULATION_INDEX) {
        return KUGLASS_MAX_MODULATION_INDEX;
    }
    return duty;
}

}  // namespace

void SpwmGenerator::begin(const PowerStagePinmap& pinmap) {
    phase_rad_ = 0.0f;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        states_[i] = SpwmChannelState{};
        blanking_remaining_s_[i] = 0.0f;
        pending_direction_[i] = false;
    }

#ifdef ESP_PLATFORM
    gpio_config_t output_config = {};
    output_config.mode = GPIO_MODE_OUTPUT;
    for (const PowerStageChannelPins& pins : pinmap.channels) {
        output_config.pin_bit_mask |=
            (1ULL << pins.direction_gpio) | (1ULL << pins.enable_gpio);
        // This dedicated disconnected-hardware test keeps raw MCU ENABLE high.
        // Preload the latch before the pad becomes an output.
        gpio_set_level(static_cast<gpio_num_t>(pins.enable_gpio),
                       KUGLASS_ENABLE_OUTPUT_ALWAYS_HIGH ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(pins.direction_gpio), 0);
    }
    ESP_ERROR_CHECK(gpio_config(&output_config));
    for (const PowerStageChannelPins& pins : pinmap.channels) {
        gpio_set_level(static_cast<gpio_num_t>(pins.enable_gpio),
                       KUGLASS_ENABLE_OUTPUT_ALWAYS_HIGH ? 1 : 0);
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
        ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config,
                                           &g_pwm[i].operator_handle));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(
            g_pwm[i].operator_handle, g_pwm[i].timer));

        mcpwm_comparator_config_t comparator_config = {};
        comparator_config.flags.update_cmp_on_tez = true;
        ESP_ERROR_CHECK(mcpwm_new_comparator(
            g_pwm[i].operator_handle, &comparator_config,
            &g_pwm[i].comparator));

        mcpwm_generator_config_t generator_config = {};
        generator_config.gen_gpio_num = pins.pwm_gpio;
        ESP_ERROR_CHECK(mcpwm_new_generator(
            g_pwm[i].operator_handle, &generator_config,
            &g_pwm[i].generator));
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
        ESP_ERROR_CHECK(
            mcpwm_comparator_set_compare_value(g_pwm[i].comparator, 0));
        // Comparator zero can still have ambiguous EMPTY/COMPARE ordering.
        // A continuous force-low is the authoritative disabled PWM state.
        ESP_ERROR_CHECK(
            mcpwm_generator_set_force_level(g_pwm[i].generator, 0, true));
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

void SpwmGenerator::set_enable_commit_callback(
    SpwmEnableCommitCallback callback,
    void* context) {
    enable_commit_callback_ = callback;
    enable_commit_context_ = context;
}

void SpwmGenerator::force_safe() {
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        set_channel_safe(i, false);
        states_[i] = SpwmChannelState{};
        blanking_remaining_s_[i] = 0.0f;
        pending_direction_[i] = false;
    }
}

void SpwmGenerator::tick(ChannelManager& channels,
                         float dt_s,
                         bool global_enable) {
    if (!std::isfinite(dt_s) || dt_s <= 0.0f) {
        force_safe();
        return;
    }

    phase_rad_ = std::fmod(
        phase_rad_ + kTwoPi * KUGLASS_FUNDAMENTAL_HZ * dt_s, kTwoPi);
    if (phase_rad_ < 0.0f) phase_rad_ += kTwoPi;
    const float sine = std::sin(phase_rad_);
    const bool positive = sine >= 0.0f;
    const float magnitude = std::fabs(sine);

    for (size_t i = 0; i < channels.count(); ++i) {
        ChannelRuntime* item = channels.channel(i);
        if (item == nullptr) {
            set_channel_safe(i, false);
            continue;
        }
        const bool enable = global_enable && item->enable && !item->faulted &&
                            item->applied_mi > 0.0f;
        apply_request(i, enable ? item->applied_mi * magnitude : 0.0f,
                      positive, enable, dt_s);
    }
}

const SpwmChannelState* SpwmGenerator::state(size_t index) const {
    return index < KUGLASS_CHANNEL_COUNT ? &states_[index] : nullptr;
}

void SpwmGenerator::apply_request(size_t index,
                                  float duty,
                                  bool positive,
                                  bool enable,
                                  float dt_s) {
    if (index >= KUGLASS_CHANNEL_COUNT) return;
    duty = clamp_duty(duty);
    const uint32_t compare_ticks = duty_to_compare_ticks(duty);
    SpwmChannelState& state = states_[index];

    if (!enable || compare_ticks == 0U ||
        compare_ticks >= kPwmPeriodTicks) {
        set_channel_safe(index, false);
        state = SpwmChannelState{};
        blanking_remaining_s_[index] = 0.0f;
        pending_direction_[index] = false;
        return;
    }

    if (state.blanking) {
        if (positive != pending_direction_[index]) {
            pending_direction_[index] = positive;
            blanking_remaining_s_[index] =
                static_cast<float>(KUGLASS_DIRECTION_BLANKING_MS) / 1000.0f;
        }
        blanking_remaining_s_[index] -= dt_s;
        if (blanking_remaining_s_[index] > 0.0f) {
            set_channel_safe(index, state.direction_positive);
            state.duty_ratio = 0.0f;
            state.enabled = false;
            return;
        }

        const bool next_direction = pending_direction_[index];
        if (set_channel_active(index, duty, next_direction)) {
            state.duty_ratio = duty;
            state.direction_positive = next_direction;
            state.enabled = true;
            state.blanking = false;
        } else {
            set_channel_safe(index, false);
            state = SpwmChannelState{};
        }
        return;
    }

    if (state.enabled && positive != state.direction_positive) {
        // Keep the old direction for the complete blanking interval. PWM is
        // forced low immediately and ENABLE is already low before DIR moves.
        set_channel_safe(index, state.direction_positive);
        pending_direction_[index] = positive;
        blanking_remaining_s_[index] =
            static_cast<float>(KUGLASS_DIRECTION_BLANKING_MS) / 1000.0f;
        state.duty_ratio = 0.0f;
        state.enabled = false;
        state.blanking = true;
        return;
    }

    if (set_channel_active(index, duty, positive)) {
        state.duty_ratio = duty;
        state.direction_positive = positive;
        state.enabled = true;
        state.blanking = false;
    } else {
        set_channel_safe(index, false);
        state = SpwmChannelState{};
    }
}

void SpwmGenerator::set_channel_safe(size_t index, bool direction_positive) {
#ifdef ESP_PLATFORM
    if (index >= KUGLASS_CHANNEL_COUNT) return;
    PwmHandle& pwm = g_pwm[index];
    // PWM and DIR still enter their safe state, but this dedicated test build
    // deliberately leaves the raw MCU ENABLE output HIGH.
    if (pwm.enable_gpio >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(pwm.enable_gpio),
                       KUGLASS_ENABLE_OUTPUT_ALWAYS_HIGH ? 1 : 0);
    }
    if (pwm.generator != nullptr) {
        (void)mcpwm_generator_set_force_level(pwm.generator, 0, true);
    }
    if (pwm.comparator != nullptr) {
        (void)mcpwm_comparator_set_compare_value(pwm.comparator, 0);
    }
    if (pwm.direction_gpio >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(pwm.direction_gpio),
                       direction_positive ? 1 : 0);
    }
#else
    (void)index;
    (void)direction_positive;
#endif
}

bool SpwmGenerator::set_channel_active(size_t index,
                                       float duty,
                                       bool direction_positive) {
    if (index >= KUGLASS_CHANNEL_COUNT) return false;
    duty = clamp_duty(duty);
#ifdef ESP_PLATFORM
    PwmHandle& pwm = g_pwm[index];
    if (pwm.generator == nullptr || pwm.comparator == nullptr ||
        pwm.enable_gpio < 0 || pwm.direction_gpio < 0) {
        return false;
    }

    const bool was_enabled = states_[index].enabled;
    if (!was_enabled) {
        gpio_set_level(static_cast<gpio_num_t>(pwm.enable_gpio),
                       KUGLASS_ENABLE_OUTPUT_ALWAYS_HIGH ? 1 : 0);
        if (mcpwm_generator_set_force_level(pwm.generator, 0, true) != ESP_OK) {
            return false;
        }
        gpio_set_level(static_cast<gpio_num_t>(pwm.direction_gpio),
                       direction_positive ? 1 : 0);
    }
    const uint32_t compare_ticks = duty_to_compare_ticks(duty);
    // compare=0 has ambiguous EMPTY/COMPARE ordering on MCPWM. It must never
    // be exposed while the stage is enabled, even during a low-MI ramp.
    if (compare_ticks == 0U || compare_ticks >= kPwmPeriodTicks) {
        return false;
    }
    if (mcpwm_comparator_set_compare_value(pwm.comparator, compare_ticks) !=
        ESP_OK) {
        return false;
    }
    if (!was_enabled) {
        if (mcpwm_generator_set_force_level(pwm.generator, -1, true) != ESP_OK) {
            return false;
        }
        const bool enabled = enable_commit_callback_ != nullptr
            ? enable_commit_callback_(index, enable_commit_context_)
            : gpio_set_level(static_cast<gpio_num_t>(pwm.enable_gpio), 1) ==
                ESP_OK;
        if (!enabled) return false;
    }
#else
    (void)direction_positive;
    if (!states_[index].enabled && enable_commit_callback_ != nullptr &&
        !enable_commit_callback_(index, enable_commit_context_)) {
        return false;
    }
#endif
    return true;
}
