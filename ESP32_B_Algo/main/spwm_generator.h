#pragma once

#include "channel_manager.h"
#include "power_stage_pinmap.h"

#include <cstddef>

class SpwmGenerator {
public:
    void begin(const PowerStagePinmap& pinmap);
    void tick(ChannelManager& channels, float dt_s, bool global_enable);

private:
    void set_channel_output(size_t index, float duty, bool positive, bool enable);
    float phase_rad_ = 0.0f;
};
