#include "adc_sampler.h"

#include "kuglass_config.h"

#include <cmath>

void AdcSampler::begin() {}

void AdcSampler::sample(ChannelManager& channels) {
    for (size_t i = 0; i < channels.count(); ++i) {
        ChannelRuntime* channel = channels.local(i);
        if (channel == nullptr) {
            continue;
        }
        channel->vrms = channel->applied_mi * KUGLASS_DC_LINK_NOMINAL_V / std::sqrt(2.0f);
        channel->irms = channel->vrms > 0.1f ? 0.018f * channel->applied_mi : 0.0f;
        channel->temp_c = 28.0f + 11.0f * channel->applied_mi * channel->applied_mi;
    }
}

