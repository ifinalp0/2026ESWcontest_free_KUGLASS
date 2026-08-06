#include "analog_monitor.h"

#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "soc/soc_caps.h"
#endif

namespace {

uint8_t descriptor_valid_bit(const AnalogInputDescriptor& descriptor) {
    return descriptor.kind == AnalogInputKind::CURRENT
        ? analog_current_valid_bit(descriptor.channel_id)
        : analog_temperature_valid_bit(descriptor.channel_id);
}

#ifdef ESP_PLATFORM
adc_atten_t descriptor_attenuation(const AnalogInputDescriptor& descriptor) {
    return descriptor.kind == AnalogInputKind::CURRENT
        ? ADC_ATTEN_DB_0 : ADC_ATTEN_DB_12;
}

bool create_curve_calibration(adc_channel_t channel,
                              adc_atten_t attenuation,
                              adc_cali_handle_t* output) {
    if (output == nullptr) return false;
    *output = nullptr;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t config = {};
    config.unit_id = ADC_UNIT_1;
    config.chan = channel;
    config.atten = attenuation;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;
    return adc_cali_create_scheme_curve_fitting(&config, output) == ESP_OK;
#else
    (void)channel;
    (void)attenuation;
    return false;
#endif
}

void delete_curve_calibration(adc_cali_handle_t* handle) {
    if (handle == nullptr || *handle == nullptr) return;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    (void)adc_cali_delete_scheme_curve_fitting(*handle);
#endif
    *handle = nullptr;
}
#endif

}  // namespace

void AnalogMedianEwmaFilter::reset() {
    initialized_ = false;
    value_ = 0;
}

bool AnalogMedianEwmaFilter::update(const int* samples,
                                    std::size_t count,
                                    int* filtered_value) {
    if (samples == nullptr || filtered_value == nullptr ||
        count != KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT) {
        return false;
    }

    int sorted[KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT] = {};
    for (std::size_t i = 0; i < count; ++i) {
        if (samples[i] < 0 || samples[i] > KUGLASS_ANALOG_RAW_MAX) return false;
        sorted[i] = samples[i];
        for (std::size_t j = i; j > 0 && sorted[j] < sorted[j - 1]; --j) {
            const int temporary = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = temporary;
        }
    }

    const int median = sorted[KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT / 2U];
    if (!initialized_) {
        value_ = median;
        initialized_ = true;
    } else {
        const int64_t weighted = static_cast<int64_t>(value_) * 7 + median;
        value_ = static_cast<int>((weighted + 4) / 8);
    }
    *filtered_value = value_;
    return true;
}

AnalogMonitor::~AnalogMonitor() {
    end();
}

bool AnalogMonitor::pinmap_matches_logic_carrier(
    const PowerStagePinmap& pinmap) {
    for (std::size_t i = 0; i < KUGLASS_ANALOG_INPUT_COUNT; ++i) {
        const AnalogInputDescriptor& descriptor = KUGLASS_ANALOG_INPUTS[i];
        if (descriptor.channel_id >= KUGLASS_CHANNEL_COUNT) return false;
        const PowerStageChannelPins& pins = pinmap.channels[descriptor.channel_id];
        const int configured_gpio = descriptor.kind == AnalogInputKind::CURRENT
            ? pins.current_adc_gpio : pins.temperature_adc_gpio;
        if (configured_gpio != descriptor.gpio) return false;
    }
    return true;
}

void AnalogMonitor::reset_state() {
    initialized_ = false;
    current_calibrated_ = false;
    temperature_calibrated_ = false;
    raw_valid_mask_ = 0;
    mv_valid_mask_ = 0;
    for (std::size_t channel = 0; channel < KUGLASS_CHANNEL_COUNT; ++channel) {
        current_raw_[channel] = 0;
        temperature_raw_[channel] = 0;
        current_mv_[channel] = 0;
        temperature_mv_[channel] = 0;
        current_sampled_at_ms_[channel] = 0;
        temperature_sampled_at_ms_[channel] = 0;
    }
    for (AnalogMedianEwmaFilter& filter : filters_) filter.reset();
}

bool AnalogMonitor::begin(const PowerStagePinmap& pinmap) {
    end();
    if (!pinmap_matches_logic_carrier(pinmap)) return false;

#ifdef ESP_PLATFORM
    for (std::size_t i = 0; i < KUGLASS_ANALOG_INPUT_COUNT; ++i) {
        adc_unit_t unit = ADC_UNIT_1;
        adc_channel_t channel = ADC_CHANNEL_0;
        const AnalogInputDescriptor& descriptor = KUGLASS_ANALOG_INPUTS[i];
        if (adc_oneshot_io_to_channel(descriptor.gpio, &unit, &channel) != ESP_OK ||
            unit != ADC_UNIT_1 ||
            static_cast<unsigned>(channel) != descriptor.adc1_channel) {
            end();
            return false;
        }
        adc_channels_[i] = channel;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    if (adc_oneshot_new_unit(&unit_config, &adc1_handle_) != ESP_OK) {
        end();
        return false;
    }

    for (std::size_t i = 0; i < KUGLASS_ANALOG_INPUT_COUNT; ++i) {
        adc_oneshot_chan_cfg_t channel_config = {};
        channel_config.atten = descriptor_attenuation(KUGLASS_ANALOG_INPUTS[i]);
        channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_oneshot_config_channel(adc1_handle_, adc_channels_[i],
                                       &channel_config) != ESP_OK) {
            end();
            return false;
        }
    }

    current_calibrated_ = create_curve_calibration(
        adc_channels_[0], ADC_ATTEN_DB_0, &current_cali_handle_);
    temperature_calibrated_ = create_curve_calibration(
        adc_channels_[1], ADC_ATTEN_DB_12, &temperature_cali_handle_);
    initialized_ = true;
    return true;
#else
    (void)pinmap;
    return false;
#endif
}

void AnalogMonitor::end() {
#ifdef ESP_PLATFORM
    delete_curve_calibration(&current_cali_handle_);
    delete_curve_calibration(&temperature_cali_handle_);
    if (adc1_handle_ != nullptr) {
        (void)adc_oneshot_del_unit(adc1_handle_);
        adc1_handle_ = nullptr;
    }
    for (adc_channel_t& channel : adc_channels_) channel = ADC_CHANNEL_0;
#endif
    reset_state();
}

void AnalogMonitor::store_raw(std::size_t input_index,
                              int raw,
                              uint32_t now_ms) {
    if (input_index >= KUGLASS_ANALOG_INPUT_COUNT) return;
    const AnalogInputDescriptor& descriptor = KUGLASS_ANALOG_INPUTS[input_index];
    const uint8_t bit = descriptor_valid_bit(descriptor);
    if (descriptor.kind == AnalogInputKind::CURRENT) {
        current_raw_[descriptor.channel_id] = raw;
        current_sampled_at_ms_[descriptor.channel_id] = now_ms;
    } else {
        temperature_raw_[descriptor.channel_id] = raw;
        temperature_sampled_at_ms_[descriptor.channel_id] = now_ms;
    }
    raw_valid_mask_ = static_cast<uint8_t>(raw_valid_mask_ | bit);
}

void AnalogMonitor::store_mv(std::size_t input_index, bool valid, int mv) {
    if (input_index >= KUGLASS_ANALOG_INPUT_COUNT) return;
    const AnalogInputDescriptor& descriptor = KUGLASS_ANALOG_INPUTS[input_index];
    const uint8_t bit = descriptor_valid_bit(descriptor);
    if (!valid) {
        mv_valid_mask_ = static_cast<uint8_t>(mv_valid_mask_ & ~bit);
        return;
    }
    if (descriptor.kind == AnalogInputKind::CURRENT) {
        current_mv_[descriptor.channel_id] = mv;
    } else {
        temperature_mv_[descriptor.channel_id] = mv;
    }
    mv_valid_mask_ = static_cast<uint8_t>(mv_valid_mask_ | bit);
}

bool AnalogMonitor::sample(uint32_t now_ms) {
#ifdef ESP_PLATFORM
    if (!initialized_ || adc1_handle_ == nullptr) return false;
    bool all_raw_updated = true;
    for (std::size_t i = 0; i < KUGLASS_ANALOG_INPUT_COUNT; ++i) {
        int discarded = 0;
        if (adc_oneshot_read(adc1_handle_, adc_channels_[i], &discarded) != ESP_OK) {
            all_raw_updated = false;
            continue;
        }

        int samples[KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT] = {};
        bool samples_ok = true;
        for (std::size_t sample_index = 0;
             sample_index < KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT;
             ++sample_index) {
            if (adc_oneshot_read(adc1_handle_, adc_channels_[i],
                                 &samples[sample_index]) != ESP_OK) {
                samples_ok = false;
                break;
            }
        }
        const AnalogInputDescriptor& descriptor = KUGLASS_ANALOG_INPUTS[i];
        const uint8_t bit = descriptor_valid_bit(descriptor);
        const uint32_t previous_sample_ms =
            descriptor.kind == AnalogInputKind::CURRENT
                ? current_sampled_at_ms_[descriptor.channel_id]
                : temperature_sampled_at_ms_[descriptor.channel_id];
        if ((raw_valid_mask_ & bit) != 0U &&
            analog_elapsed_ms(now_ms, previous_sample_ms) >
                KUGLASS_ANALOG_DEFAULT_MAX_AGE_MS) {
            // Do not blend a newly recovered input with a value that has
            // already aged out of telemetry validity.
            filters_[i].reset();
        }
        int filtered_raw = 0;
        if (!samples_ok || !filters_[i].update(
                samples, KUGLASS_ANALOG_MEDIAN_SAMPLE_COUNT, &filtered_raw)) {
            all_raw_updated = false;
            continue;
        }

        store_raw(i, filtered_raw, now_ms);
        adc_cali_handle_t calibration = descriptor.kind == AnalogInputKind::CURRENT
            ? current_cali_handle_ : temperature_cali_handle_;
        int mv = 0;
        const bool mv_valid = calibration != nullptr &&
            adc_cali_raw_to_voltage(calibration, filtered_raw, &mv) == ESP_OK;
        store_mv(i, mv_valid, mv);
    }
    return all_raw_updated;
#else
    (void)now_ms;
    return false;
#endif
}

AnalogTelemetrySnapshot AnalogMonitor::snapshot(uint32_t now_ms,
                                                uint32_t max_age_ms) const {
    AnalogTelemetrySnapshot result;
    result.initialized = initialized_;
    result.current_calibrated = current_calibrated_;
    result.temperature_calibrated = temperature_calibrated_;
    result.raw_valid_mask = raw_valid_mask_;
    result.mv_valid_mask = mv_valid_mask_;

    for (std::size_t channel = 0; channel < KUGLASS_CHANNEL_COUNT; ++channel) {
        result.current_raw[channel] = current_raw_[channel];
        result.temperature_raw[channel] = temperature_raw_[channel];
        result.current_mv[channel] = current_mv_[channel];
        result.temperature_mv[channel] = temperature_mv_[channel];
        result.current_sampled_at_ms[channel] = current_sampled_at_ms_[channel];
        result.temperature_sampled_at_ms[channel] =
            temperature_sampled_at_ms_[channel];

        const uint8_t current_bit = analog_current_valid_bit(channel);
        const bool current_fresh = analog_timestamp_is_fresh(
            (raw_valid_mask_ & current_bit) != 0U, now_ms,
            current_sampled_at_ms_[channel], max_age_ms);
        if (!current_fresh) {
            result.raw_valid_mask = static_cast<uint8_t>(
                result.raw_valid_mask & ~current_bit);
            result.mv_valid_mask = static_cast<uint8_t>(
                result.mv_valid_mask & ~current_bit);
        }

        const uint8_t temperature_bit = analog_temperature_valid_bit(channel);
        const bool temperature_fresh = analog_timestamp_is_fresh(
            (raw_valid_mask_ & temperature_bit) != 0U, now_ms,
            temperature_sampled_at_ms_[channel], max_age_ms);
        if (!temperature_fresh) {
            result.raw_valid_mask = static_cast<uint8_t>(
                result.raw_valid_mask & ~temperature_bit);
            result.mv_valid_mask = static_cast<uint8_t>(
                result.mv_valid_mask & ~temperature_bit);
        }
    }
    result.mv_valid_mask = static_cast<uint8_t>(
        result.mv_valid_mask & result.raw_valid_mask);
    return result;
}
