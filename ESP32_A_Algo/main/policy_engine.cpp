#include "policy_engine.h"

#include "kuglass_config.h"

#include <cmath>

namespace {

constexpr float kStrongGlareThreshold = 0.46f;

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float roi_glare_score(const CameraRoiMetrics& roi,
                      bool ae_metadata_valid,
                      float exposure_us,
                      float analog_gain,
                      float digital_gain) {
    float exposure_pressure = 0.0f;
    float gain_pressure = 0.0f;
    if (ae_metadata_valid) {
        const float exposure = exposure_us < 100.0f ? 100.0f : exposure_us;
        const float gain = analog_gain * digital_gain < 0.05f
                               ? 0.05f
                               : analog_gain * digital_gain;
        exposure_pressure = clamp01((12000.0f / exposure - 0.75f) / 3.0f);
        gain_pressure = clamp01(1.5f / gain - 0.25f);
    }
    float edge_penalty = (0.08f - roi.edge_density) / 0.08f;
    if (edge_penalty < 0.0f) edge_penalty = 0.0f;
    if (edge_penalty > 0.25f) edge_penalty = 0.25f;
    return clamp01(0.62f * roi.saturation_ratio +
                   0.18f * roi.highlight_area +
                   0.12f * exposure_pressure +
                   0.08f * gain_pressure + edge_penalty);
}

float thermal_weight(uint8_t channel) {
    constexpr float values[KUGLASS_TOTAL_CHANNELS] = {0.22f, 0.24f, 0.58f, 0.58f};
    return values[channel];
}

float camera_weight(uint8_t channel) {
    return channel < 2U ? 0.82f : 0.08f;
}

float visibility_penalty(uint8_t channel, VehicleMode mode) {
    if (mode == VehicleMode::CAMPING || mode == VehicleMode::PARKED) return 0.0f;
    return channel < 2U ? 0.68f : 0.18f;
}

float visibility_weight(uint8_t channel) {
    return channel < 2U ? 0.34f : 0.10f;
}

float transmission_to_mi(float transmission) {
    struct LutPoint { float transmission; float mi; };
    constexpr LutPoint points[] = {
        {0.00f, 0.00f}, {0.15f, 0.18f}, {0.35f, 0.40f},
        {0.65f, 0.68f}, {0.85f, 0.84f}, {1.00f, 0.95f},
    };
    const float value = clamp01(transmission);
    for (size_t i = 1; i < sizeof(points) / sizeof(points[0]); ++i) {
        if (value <= points[i].transmission) {
            const float ratio = (value - points[i - 1].transmission) /
                                (points[i].transmission - points[i - 1].transmission);
            return clamp01(points[i - 1].mi + ratio * (points[i].mi - points[i - 1].mi));
        }
    }
    return points[sizeof(points) / sizeof(points[0]) - 1].mi;
}

OpticalState optical_state_for_transmission(float transmission) {
    if (transmission >= 0.74f) return OpticalState::CLEAR;
    if (transmission >= 0.24f) return OpticalState::DIM;
    return OpticalState::FROST;
}

bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void copy_environment_value(uint32_t field,
                            const EnvironmentPatch& source,
                            EnvironmentPatch* destination) {
    destination->present_mask |= field;
    if ((source.value_mask & field) != 0U) {
        destination->value_mask |= field;
    } else {
        destination->value_mask &= ~field;
    }
    switch (field) {
        case ENV_INTERNAL_TEMP:
            destination->internal_temp_c = source.internal_temp_c;
            break;
        case ENV_FRONT_LEFT_SATURATION:
            destination->front_left_saturation = source.front_left_saturation;
            break;
        case ENV_FRONT_RIGHT_SATURATION:
            destination->front_right_saturation = source.front_right_saturation;
            break;
        case ENV_EDGE_DENSITY:
            destination->edge_density = source.edge_density;
            break;
        default:
            break;
    }
}

}  // namespace

void PolicyEngine::begin() {
    vehicle_mode_ = VehicleMode::DRIVING;
    demo_mode_ = DemoMode::NONE;
    for (size_t i = 0; i < KUGLASS_TOTAL_CHANNELS; ++i) {
        manual_[i] = ManualState{};
        servo_initialized_[i] = false;
        servo_mi_[i] = 0.0f;
    }
    diagnostic_fault_mask_ = 0;
    diagnostic_environment_ = EnvironmentPatch{};
    has_last_command_seq_ = false;
    last_command_seq_ = 0;
    decision_seq_ = 0;
    last_update_ms_ = 0;
}

bool PolicyEngine::accept_sequence(const UiCommand& command) {
    if (!command.has_seq) return true;
    if (has_last_command_seq_ &&
        static_cast<int32_t>(command.seq - last_command_seq_) <= 0) {
        return false;
    }
    has_last_command_seq_ = true;
    last_command_seq_ = command.seq;
    return true;
}

PolicyApplyResult PolicyEngine::apply_command(const UiCommand& command, uint32_t now_ms) {
    if (!accept_sequence(command)) return {false, "STALE_SEQUENCE"};

    switch (command.type) {
        case UiCommandType::SET_MODE:
            vehicle_mode_ = command.mode;
            return {true, nullptr};
        case UiCommandType::SET_DEMO:
            for (ManualState& manual : manual_) manual = ManualState{};
            demo_mode_ = command.demo_mode;
            if (demo_mode_ == DemoMode::CAMPING) vehicle_mode_ = VehicleMode::CAMPING;
            if (demo_mode_ == DemoMode::PARKED) vehicle_mode_ = VehicleMode::PARKED;
            if (demo_mode_ == DemoMode::HOT_SUMMER ||
                demo_mode_ == DemoMode::CAMERA_SATURATION) {
                vehicle_mode_ = VehicleMode::DRIVING;
            }
            return {true, nullptr};
        case UiCommandType::MANUAL_CHANNEL: {
            ManualState& manual = manual_[command.channel_id];
            manual.active = true;
            manual.target_mi = clamp01(command.target_mi);
            manual.enable = command.enable;
            manual.expires_ms = now_ms + command.ttl_ms;
            return {true, nullptr};
        }
        case UiCommandType::RETURN_AUTO:
            if (command.has_channel_id) {
                manual_[command.channel_id] = ManualState{};
            } else {
                for (ManualState& manual : manual_) manual = ManualState{};
            }
            return {true, nullptr};
        case UiCommandType::RESET_FAULT:
            return {true, nullptr};
        case UiCommandType::SET_ENVIRONMENT:
            if (!KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS) {
                return {false, "DIAGNOSTIC_DISABLED"};
            }
            apply_environment_patch(command.environment);
            return {true, nullptr};
        case UiCommandType::SET_CHANNEL_FAULT:
            if (!KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS) {
                return {false, "DIAGNOSTIC_DISABLED"};
            }
            if (command.fault) {
                diagnostic_fault_mask_ |= static_cast<uint8_t>(1U << command.channel_id);
            } else {
                diagnostic_fault_mask_ &=
                    static_cast<uint8_t>(~(1U << command.channel_id));
            }
            return {true, nullptr};
        case UiCommandType::CAMERA_STREAM:
            // Streaming is an auxiliary TabUI concern. Accept its sequence
            // here so the global UI command ordering remains authoritative,
            // but do not alter policy, channel targets, or sensor overrides.
            return {true, nullptr};
        default:
            return {false, "UNSUPPORTED_COMMAND"};
    }
}

void PolicyEngine::apply_environment_patch(const EnvironmentPatch& patch) {
    for (uint32_t field = ENV_INTERNAL_TEMP; field <= ENV_EDGE_DENSITY; field <<= 1U) {
        if ((patch.present_mask & field) != 0U) {
            copy_environment_value(field, patch, &diagnostic_environment_);
        }
    }
}

SensorSnapshot PolicyEngine::effective_sensors(const SensorSnapshot& physical,
                                               uint32_t now_ms) const {
    SensorSnapshot result = physical;
#if KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS
    const EnvironmentPatch& patch = diagnostic_environment_;
    const auto has_value = [&patch](uint32_t field) {
        return (patch.present_mask & field) != 0U &&
               (patch.value_mask & field) != 0U;
    };
    const auto was_cleared = [&patch](uint32_t field) {
        return (patch.present_mask & field) != 0U &&
               (patch.value_mask & field) == 0U;
    };

    if (has_value(ENV_INTERNAL_TEMP)) {
        result.internal_temp_c = patch.internal_temp_c;
        result.internal_temp_valid = true;
        result.internal_temp_timestamp_ms = now_ms;
    } else if (was_cleared(ENV_INTERNAL_TEMP)) {
        result.internal_temp_valid = false;
    }

    const uint32_t camera_fields = ENV_FRONT_LEFT_SATURATION |
                                   ENV_FRONT_RIGHT_SATURATION |
                                   ENV_EDGE_DENSITY;
    if ((patch.present_mask & camera_fields) != 0U) {
        if (has_value(ENV_FRONT_LEFT_SATURATION)) {
            result.front_left.saturation_ratio = patch.front_left_saturation;
        }
        if (has_value(ENV_FRONT_RIGHT_SATURATION)) {
            result.front_right.saturation_ratio = patch.front_right_saturation;
        }
        if (has_value(ENV_EDGE_DENSITY)) {
            result.front_left.edge_density = patch.edge_density;
            result.front_right.edge_density = patch.edge_density;
        }
        result.camera_valid = (patch.value_mask & camera_fields) != 0U;
        result.camera_timestamp_ms = now_ms;
    }
#else
    (void)now_ms;
#endif
    return result;
}

PolicyDecision PolicyEngine::update(const SensorSnapshot& physical_sensors,
                                    uint32_t now_ms) {
    const SensorSnapshot sensors = effective_sensors(physical_sensors, now_ms);
    PolicyDecision decision;
    decision.seq = ++decision_seq_;
    decision.vehicle_mode = vehicle_mode_;
    decision.demo_mode = demo_mode_;

    const bool camera_fresh = sensors.camera_valid &&
        timestamp_fresh(now_ms, sensors.camera_timestamp_ms, KUGLASS_CAMERA_STALE_MS);
    const bool temperature_fresh = sensors.internal_temp_valid &&
        timestamp_fresh(now_ms, sensors.internal_temp_timestamp_ms,
                        KUGLASS_TEMPERATURE_STALE_MS);

    if (camera_fresh) {
        decision.glare_left = roi_glare_score(
            sensors.front_left, sensors.ae_metadata_valid, sensors.exposure_us,
            sensors.analog_gain, sensors.digital_gain);
        decision.glare_right = roi_glare_score(
            sensors.front_right, sensors.ae_metadata_valid, sensors.exposure_us,
            sensors.analog_gain, sensors.digital_gain);
        decision.glare_total = decision.glare_left > decision.glare_right
                                   ? decision.glare_left
                                   : decision.glare_right;
    }
    decision.strong_front_light = camera_fresh &&
                                  decision.glare_total >= kStrongGlareThreshold;
    decision.thermal_risk = temperature_fresh
        ? clamp01((sensors.internal_temp_c - 27.0f) / 15.0f)
        : 0.0f;

    if (demo_mode_ == DemoMode::HOT_SUMMER && temperature_fresh) {
        decision.thermal_risk = decision.thermal_risk < 0.8f
                                    ? 0.8f
                                    : decision.thermal_risk;
    }
    if (demo_mode_ == DemoMode::CAMERA_SATURATION && camera_fresh) {
        decision.glare_left = decision.glare_left < 0.8f ? 0.8f : decision.glare_left;
        decision.glare_right = decision.glare_right < 0.8f ? 0.8f : decision.glare_right;
        decision.glare_total = decision.glare_left > decision.glare_right
                                   ? decision.glare_left
                                   : decision.glare_right;
        decision.strong_front_light = true;
    }

    const float privacy_need = vehicle_mode_ == VehicleMode::CAMPING
        ? 1.0f
        : vehicle_mode_ == VehicleMode::PARKED ? 0.82f : 0.0f;
    const float mode_need = demo_mode_ == DemoMode::HOT_SUMMER ? 0.35f : 0.0f;

    const float dt_s = last_update_ms_ == 0U
        ? static_cast<float>(KUGLASS_CONTROL_PERIOD_MS) / 1000.0f
        : static_cast<float>(static_cast<uint32_t>(now_ms - last_update_ms_)) / 1000.0f;
    last_update_ms_ = now_ms;

    bool any_fault = false;
    bool any_manual = false;
    for (uint8_t channel = 0; channel < KUGLASS_TOTAL_CHANNELS; ++channel) {
        PolicyChannelTarget& target = decision.channels[channel];
        target.channel_id = channel;

        ManualState& manual = manual_[channel];
        if (manual.active && time_reached(now_ms, manual.expires_ms)) {
            manual = ManualState{};
        }
        const bool diagnostic_fault =
            (diagnostic_fault_mask_ & static_cast<uint8_t>(1U << channel)) != 0U;

        if (diagnostic_fault) {
            target.enable = false;
            target.fault = true;
            target.target_mi = 0.0f;
            target.target_transmission = 0.0f;
            target.optical_state = OpticalState::FROST;
            target.reason = "fault: output disabled";
            any_fault = true;
        } else if (manual.active) {
            target.manual = true;
            target.manual_until_ms = manual.expires_ms;
            target.enable = manual.enable;
            target.target_mi = manual.enable ? manual.target_mi : 0.0f;
            target.target_transmission = clamp01(target.target_mi);
            target.optical_state = optical_state_for_transmission(target.target_transmission);
            target.reason = "manual TTL override";
            any_manual = true;
        } else {
            const float camera = channel == 0U
                ? decision.glare_left
                : channel == 1U ? decision.glare_right : decision.glare_total;
            const float score = clamp01(
                0.22f * mode_need +
                thermal_weight(channel) * decision.thermal_risk +
                camera_weight(channel) * camera +
                0.90f * privacy_need -
                visibility_weight(channel) * visibility_penalty(channel, vehicle_mode_));

            float transmission = clamp01(1.0f - 0.82f * score);
            if (vehicle_mode_ == VehicleMode::DRIVING && channel < 2U &&
                transmission < 0.45f) {
                transmission = 0.45f;
            }
            if (privacy_need > 0.0f) {
                const float privacy_transmission = vehicle_mode_ == VehicleMode::CAMPING
                    ? 0.08f : 0.16f;
                if (transmission > privacy_transmission) transmission = privacy_transmission;
            }

            target.score = score;
            target.target_transmission = transmission;
            target.target_mi = transmission_to_mi(transmission);
            target.optical_state = optical_state_for_transmission(transmission);
            if (privacy_need > 0.0f) target.reason = "privacy mode";
            else if (channel < 2U && camera_fresh && camera > 0.2f) {
                target.reason = "camera glare response";
            } else if (temperature_fresh && decision.thermal_risk > 0.2f) {
                target.reason = "internal temperature response";
            } else if (!camera_fresh && !temperature_fresh) {
                target.reason = "sensor input unavailable";
            } else {
                target.reason = "automatic clear";
            }
        }

        const float desired_mi = target.enable ? target.target_mi : 0.0f;
        if (!servo_initialized_[channel]) {
            servo_mi_[channel] = desired_mi;
            servo_initialized_[channel] = true;
        } else {
            const float previous = servo_mi_[channel];
            const bool hold_camera_noise =
                !target.manual && camera_fresh && target.enable && !target.fault &&
                std::fabs(desired_mi - previous) <= KUGLASS_MI_AUTO_DEADBAND;
            const bool fast_attack = channel < 2U && decision.strong_front_light &&
                                     desired_mi < previous - 0.18f;
            float limited = hold_camera_noise ? previous : desired_mi;
            if (!hold_camera_noise && !fast_attack && desired_mi < previous) {
                limited = previous - KUGLASS_MI_ATTACK_PER_S * dt_s;
                if (limited < desired_mi) limited = desired_mi;
            } else if (!hold_camera_noise && desired_mi > previous) {
                limited = previous + KUGLASS_MI_RELEASE_PER_S * dt_s;
                if (limited > desired_mi) limited = desired_mi;
            }
            servo_mi_[channel] = fast_attack
                ? desired_mi
                : clamp01(previous +
                          KUGLASS_MI_SERVO_RESPONSE * (limited - previous));
        }
        target.applied_mi = servo_mi_[channel];
    }

    if (any_fault) decision.decision_reason = "fault: output disabled";
    else if (any_manual) decision.decision_reason = "manual TTL override";
    else if (privacy_need > 0.0f) decision.decision_reason = "privacy mode";
    else if (decision.strong_front_light) {
        decision.decision_reason = "camera glare: front fast response";
    } else if (decision.thermal_risk > 0.2f) {
        decision.decision_reason = "internal temperature: thermal load response";
    } else if (!camera_fresh && !temperature_fresh) {
        decision.decision_reason = "camera and temperature unavailable";
    } else {
        decision.decision_reason = "automatic clear";
    }
    return decision;
}

bool policy_decision_to_protocol(const PolicyDecision& decision,
                                 uint32_t ttl_ms,
                                 ProtocolCommand* command) {
    if (command == nullptr) return false;
    *command = ProtocolCommand{};
    command->seq = decision.seq;
    command->ttl_ms = ttl_ms;
    command->channel_count = KUGLASS_TOTAL_CHANNELS;
    for (size_t i = 0; i < KUGLASS_TOTAL_CHANNELS; ++i) {
        command->channels[i].channel_id = decision.channels[i].channel_id;
        command->channels[i].mi = decision.channels[i].applied_mi;
        command->channels[i].enable = decision.channels[i].enable &&
                                      !decision.channels[i].fault;
    }
    return true;
}

const char* optical_state_name(OpticalState state) {
    switch (state) {
        case OpticalState::CLEAR: return "CLEAR";
        case OpticalState::DIM: return "DIM";
        case OpticalState::FROST: return "FROST";
        default: return "FROST";
    }
}
