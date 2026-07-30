#include "channel_manager.h"

namespace {

float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

}  // namespace

void ChannelManager::begin(uint8_t first_channel) {
    first_channel_ = first_channel;
    for (size_t i = 0; i < KUGLASS_LOCAL_CHANNELS; ++i) {
        channels_[i] = ChannelRuntime{};
        channels_[i].global_id = static_cast<uint8_t>(first_channel_ + i);
    }
}

void ChannelManager::apply_command(const ProtocolCommand& command) {
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& item = command.channels[i];
        int local_index = local_index_for_global(item.channel_id);
        if (local_index < 0) {
            continue;
        }
        ChannelRuntime& channel = channels_[local_index];
        channel.target_mi = clamp01(item.mi);
        channel.enable = item.enable;
    }
}

void ChannelManager::update(float dt_s, bool global_enable) {
    for (ChannelRuntime& channel : channels_) {
        const float desired = (global_enable && channel.enable && !channel.faulted) ? channel.target_mi : 0.0f;
        if (desired < channel.applied_mi) {
            channel.applied_mi -= KUGLASS_MI_ATTACK_PER_S * dt_s;
            if (channel.applied_mi < desired) {
                channel.applied_mi = desired;
            }
        } else {
            channel.applied_mi += KUGLASS_MI_RELEASE_PER_S * dt_s;
            if (channel.applied_mi > desired) {
                channel.applied_mi = desired;
            }
        }
        channel.applied_mi = clamp01(channel.applied_mi);
    }
}

ChannelRuntime* ChannelManager::local(size_t index) {
    if (index >= KUGLASS_LOCAL_CHANNELS) {
        return nullptr;
    }
    return &channels_[index];
}

const ChannelRuntime* ChannelManager::local(size_t index) const {
    if (index >= KUGLASS_LOCAL_CHANNELS) {
        return nullptr;
    }
    return &channels_[index];
}

int ChannelManager::local_index_for_global(uint8_t global_id) const {
    if (global_id < first_channel_ || global_id >= first_channel_ + KUGLASS_LOCAL_CHANNELS) {
        return -1;
    }
    return static_cast<int>(global_id - first_channel_);
}

