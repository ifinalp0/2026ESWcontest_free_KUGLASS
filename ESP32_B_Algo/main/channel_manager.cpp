#include "channel_manager.h"

namespace {

static_assert(KUGLASS_CHANNEL_COUNT == KUGLASS_MAX_COMMAND_CHANNELS,
              "ESP32_A and ESP32_B channel contracts must match.");

float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

}  // namespace

void ChannelManager::begin() {
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        channels_[i] = ChannelRuntime{};
        channels_[i].channel_id = static_cast<uint8_t>(i);
    }
}

void ChannelManager::apply_command(const ProtocolCommand& command) {
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& item = command.channels[i];
        if (item.channel_id >= KUGLASS_CHANNEL_COUNT) continue;
        ChannelRuntime& target = channels_[item.channel_id];
        target.target_mi = clamp01(item.mi);
        target.enable = item.enable;
    }
}

void ChannelManager::update(float dt_s, bool global_enable) {
    for (ChannelRuntime& item : channels_) {
        const float desired = global_enable && item.enable && !item.faulted
            ? item.target_mi : 0.0f;
        if (desired < item.applied_mi) {
            item.applied_mi -= KUGLASS_MI_ATTACK_PER_S * dt_s;
            if (item.applied_mi < desired) item.applied_mi = desired;
        } else {
            item.applied_mi += KUGLASS_MI_RELEASE_PER_S * dt_s;
            if (item.applied_mi > desired) item.applied_mi = desired;
        }
        item.applied_mi = clamp01(item.applied_mi);
    }
}

void ChannelManager::set_fault(size_t index, bool faulted) {
    if (index < KUGLASS_CHANNEL_COUNT) channels_[index].faulted = faulted;
}

ChannelRuntime* ChannelManager::channel(size_t index) {
    return index < KUGLASS_CHANNEL_COUNT ? &channels_[index] : nullptr;
}

const ChannelRuntime* ChannelManager::channel(size_t index) const {
    return index < KUGLASS_CHANNEL_COUNT ? &channels_[index] : nullptr;
}
