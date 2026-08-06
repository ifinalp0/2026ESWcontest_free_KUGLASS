#pragma once

#include "power_stage_pinmap.h"

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#endif

static constexpr std::size_t KUGLASS_ANALOG_INPUT_COUNT =
    KUGLASS_CHANNEL_COUNT * 2U;
static constexpr std::size_t KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT = 5U;
static constexpr uint32_t KUGLASS_ANALOG_DEFAULT_MAX_AGE_MS = 100U;
static constexpr int KUGLASS_ANALOG_RAW_MAX = 4095;

enum class AnalogInputKind : uint8_t {
    CURRENT = 0,
    TEMPERATURE,
};

struct AnalogInputDescriptor {
    uint8_t channel_id;
    AnalogInputKind kind;
    int gpio;
    uint8_t adc1_channel;
};

// Logic Carrier U3 ADC wiring. adc1_channel values intentionally remain plain
// integers so the pin contract can also be validated by host tests without
// including ESP-IDF headers.
static constexpr AnalogInputDescriptor KUGLASS_ANALOG_INPUTS[
    KUGLASS_ANALOG_INPUT_COUNT] = {
    {0, AnalogInputKind::CURRENT, 1, 0},
    {0, AnalogInputKind::TEMPERATURE, 2, 1},
    {1, AnalogInputKind::CURRENT, 4, 3},
    {1, AnalogInputKind::TEMPERATURE, 5, 4},
    {2, AnalogInputKind::CURRENT, 6, 5},
    {2, AnalogInputKind::TEMPERATURE, 7, 6},
    {3, AnalogInputKind::CURRENT, 8, 7},
    {3, AnalogInputKind::TEMPERATURE, 3, 2},
};

constexpr uint8_t analog_current_valid_bit(std::size_t channel) {
    return channel < KUGLASS_CHANNEL_COUNT
        ? static_cast<uint8_t>(1U << channel) : 0U;
}

constexpr uint8_t analog_temperature_valid_bit(std::size_t channel) {
    return channel < KUGLASS_CHANNEL_COUNT
        ? static_cast<uint8_t>(1U << (channel + KUGLASS_CHANNEL_COUNT)) : 0U;
}

constexpr uint32_t analog_elapsed_ms(uint32_t now_ms,
                                     uint32_t sampled_at_ms) {
    return static_cast<uint32_t>(now_ms - sampled_at_ms);
}

constexpr bool analog_timestamp_is_fresh(bool valid,
                                         uint32_t now_ms,
                                         uint32_t sampled_at_ms,
                                         uint32_t max_age_ms) {
    return valid && analog_elapsed_ms(now_ms, sampled_at_ms) <= max_age_ms;
}

struct AnalogTelemetrySnapshot {
    bool initialized = false;
    bool current_calibrated = false;
    bool temperature_calibrated = false;
    uint8_t raw_valid_mask = 0;
    uint8_t mv_valid_mask = 0;
    int current_raw[KUGLASS_CHANNEL_COUNT] = {};
    int temperature_raw[KUGLASS_CHANNEL_COUNT] = {};
    int current_mv[KUGLASS_CHANNEL_COUNT] = {};
    int temperature_mv[KUGLASS_CHANNEL_COUNT] = {};
    uint32_t current_sampled_at_ms[KUGLASS_CHANNEL_COUNT] = {};
    uint32_t temperature_sampled_at_ms[KUGLASS_CHANNEL_COUNT] = {};
};

// Five-sample median followed by an integer EWMA with alpha=1/8. This helper
// contains no ESP-IDF dependencies so filtering and rounding are host-testable.
class AnalogMedianEwmaFilter {
public:
    void reset();
    bool update(const int* samples, std::size_t count, int* filtered_value);
    bool initialized() const { return initialized_; }
    int value() const { return value_; }

private:
    bool initialized_ = false;
    int value_ = 0;
};

// Owns the ADC1 oneshot and calibration handles only. It intentionally creates
// no task and uses no lock; the caller must serialize begin/sample/snapshot/end.
class AnalogMonitor {
public:
    AnalogMonitor() = default;
    ~AnalogMonitor();

    AnalogMonitor(const AnalogMonitor&) = delete;
    AnalogMonitor& operator=(const AnalogMonitor&) = delete;

    // Returns true when all eight raw inputs are configured. Calibration is
    // optional and does not affect this return value.
    bool begin(const PowerStagePinmap& pinmap);
    void end();

    // Performs one scan. Each input gets one settling conversion followed by a
    // five-sample median. Returns true only if all eight raw inputs were updated;
    // successfully read inputs still update when another input fails.
    bool sample(uint32_t now_ms);

    // Validity is pruned using unsigned subtraction, so timestamp age remains
    // correct across the uint32_t millisecond wraparound.
    AnalogTelemetrySnapshot snapshot(
        uint32_t now_ms,
        uint32_t max_age_ms = KUGLASS_ANALOG_DEFAULT_MAX_AGE_MS) const;

    bool initialized() const { return initialized_; }
    bool current_calibrated() const { return current_calibrated_; }
    bool temperature_calibrated() const { return temperature_calibrated_; }

    static bool pinmap_matches_logic_carrier(const PowerStagePinmap& pinmap);

private:
    void reset_state();
    void store_raw(std::size_t input_index, int raw, uint32_t now_ms);
    void store_mv(std::size_t input_index, bool valid, int mv);

    bool initialized_ = false;
    bool current_calibrated_ = false;
    bool temperature_calibrated_ = false;
    uint8_t raw_valid_mask_ = 0;
    uint8_t mv_valid_mask_ = 0;
    int current_raw_[KUGLASS_CHANNEL_COUNT] = {};
    int temperature_raw_[KUGLASS_CHANNEL_COUNT] = {};
    int current_mv_[KUGLASS_CHANNEL_COUNT] = {};
    int temperature_mv_[KUGLASS_CHANNEL_COUNT] = {};
    uint32_t current_sampled_at_ms_[KUGLASS_CHANNEL_COUNT] = {};
    uint32_t temperature_sampled_at_ms_[KUGLASS_CHANNEL_COUNT] = {};
    AnalogMedianEwmaFilter filters_[KUGLASS_ANALOG_INPUT_COUNT];

#ifdef ESP_PLATFORM
    adc_oneshot_unit_handle_t adc1_handle_ = nullptr;
    adc_channel_t adc_channels_[KUGLASS_ANALOG_INPUT_COUNT] = {};
    adc_cali_handle_t current_cali_handle_ = nullptr;
    adc_cali_handle_t temperature_cali_handle_ = nullptr;
#endif
};
