#pragma once

#include "channel_manager.h"
#include "pinmap.h"

#include <cstddef>

class SpwmGenerator {
public:
    void begin(const CarrierPinmap& pinmap);
    void tick(ChannelManager& channels, float dt_s, bool global_enable);

private:
    void set_channel_output(size_t local_index, float duty, bool polarity_positive, bool enable);
    float phase_rad_ = 0.0f;
};

