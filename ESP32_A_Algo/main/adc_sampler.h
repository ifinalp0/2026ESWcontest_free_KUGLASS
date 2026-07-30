#pragma once

#include "channel_manager.h"

class AdcSampler {
public:
    void begin();
    void sample(ChannelManager& channels);
};

