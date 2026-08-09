#pragma once

#include "kuglass_config.h"
#include "protocol.h"
#include "sensor_snapshot.h"
#include "ui_protocol.h"

#include <cstddef>
#include <cstdint>

enum class OpticalState : uint8_t {
    CLEAR = 0,
    DIM,
    FROST,
};

struct PolicyChannelTarget {
    uint8_t channel_id = 0;
    float target_transmission = 0.0f;
    float target_mi = 0.0f;
    float applied_mi = 0.0f;  // MI command after the master-side safe servo.
    float score = 0.0f;
    bool enable = true;
    bool fault = false;
    bool manual = false;
    bool manual_persistent = false;
    uint32_t manual_until_ms = 0;
    OpticalState optical_state = OpticalState::FROST;
    const char* reason = "automatic clear";
};

struct PolicyDecision {
    uint32_t seq = 0;
    VehicleMode vehicle_mode = VehicleMode::DRIVING;
    DemoMode demo_mode = DemoMode::NONE;
    float thermal_risk = 0.0f;
    float glare_left = 0.0f;
    float glare_right = 0.0f;
    float glare_total = 0.0f;
    bool strong_front_light = false;
    PolicyChannelTarget channels[KUGLASS_TOTAL_CHANNELS];
    const char* decision_reason = "automatic clear";
};

struct PolicyApplyResult {
    bool accepted = false;
    const char* error = nullptr;
};

class PolicyEngine {
public:
    void begin();
    PolicyApplyResult apply_command(const UiCommand& command, uint32_t now_ms);
    PolicyDecision update(const SensorSnapshot& physical_sensors, uint32_t now_ms);
    SensorSnapshot effective_sensors(const SensorSnapshot& physical_sensors,
                                     uint32_t now_ms) const;

private:
    struct ManualState {
        bool active = false;
        float target_mi = 0.0f;
        bool enable = true;
        bool persistent = false;
        uint32_t expires_ms = 0;
    };

    bool accept_sequence(const UiCommand& command);
    void apply_environment_patch(const EnvironmentPatch& patch);

    VehicleMode vehicle_mode_ = VehicleMode::DRIVING;
    DemoMode demo_mode_ = DemoMode::NONE;
    ManualState manual_[KUGLASS_TOTAL_CHANNELS];
    uint8_t diagnostic_fault_mask_ = 0;
    EnvironmentPatch diagnostic_environment_;
    bool has_last_command_seq_ = false;
    uint32_t last_command_seq_ = 0;
    uint32_t decision_seq_ = 0;
    uint32_t last_update_ms_ = 0;
    bool servo_initialized_[KUGLASS_TOTAL_CHANNELS] = {};
    float servo_mi_[KUGLASS_TOTAL_CHANNELS] = {};
};

bool policy_decision_to_protocol(const PolicyDecision& decision,
                                 uint32_t ttl_ms,
                                 ProtocolCommand* command);
const char* optical_state_name(OpticalState state);
