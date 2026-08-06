#pragma once

#include "channel_manager.h"
#include "power_stage_pinmap.h"

#include <cstddef>

struct SpwmChannelState {
    float duty_ratio = 0.0f;
    bool direction_positive = false;
    bool enabled = false;
    bool blanking = false;
};

using SpwmEnableCommitCallback = bool (*)(size_t channel_index,
                                          void* context);

class SpwmGenerator {
public:
    void begin(const PowerStagePinmap& pinmap);
    void set_enable_commit_callback(SpwmEnableCommitCallback callback,
                                    void* context);
    void force_safe();
    void tick(ChannelManager& channels, float dt_s, bool global_enable);
    const SpwmChannelState* state(size_t index) const;

private:
    void apply_request(size_t index,
                       float duty,
                       bool positive,
                       bool enable,
                       float dt_s);
    void set_channel_safe(size_t index, bool direction_positive);
    bool set_channel_active(size_t index, float duty, bool direction_positive);

    float phase_rad_ = 0.0f;
    SpwmChannelState states_[KUGLASS_CHANNEL_COUNT] = {};
    float blanking_remaining_s_[KUGLASS_CHANNEL_COUNT] = {};
    bool pending_direction_[KUGLASS_CHANNEL_COUNT] = {};
    SpwmEnableCommitCallback enable_commit_callback_ = nullptr;
    void* enable_commit_context_ = nullptr;
};
