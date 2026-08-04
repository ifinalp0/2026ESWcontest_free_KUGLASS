#include "channel_manager.h"

#include <cmath>

namespace {

static_assert(KUGLASS_CHANNEL_COUNT == KUGLASS_MAX_COMMAND_CHANNELS,
              "ESP32_A and ESP32_B channel contracts must match.");

float clamp_mi(float value) {
    if (!std::isfinite(value)) return 0.0f;
    if (value < 0.0f) return 0.0f;
    if (value > KUGLASS_MAX_MODULATION_INDEX) {
        return KUGLASS_MAX_MODULATION_INDEX;
    }
    return value;
}

}  // namespace

void ChannelManager::begin() {
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        channels_[i] = ChannelRuntime{};
        channels_[i].channel_id = static_cast<uint8_t>(i);
    }
}

bool ChannelManager::apply_command(const ProtocolCommand& command) {
    if (command.channel_count != KUGLASS_CHANNEL_COUNT) return false;
    uint8_t seen = 0;
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& item = command.channels[i];
        if (item.channel_id >= KUGLASS_CHANNEL_COUNT ||
            !std::isfinite(item.mi) || item.mi < 0.0f || item.mi > 1.0f) {
            return false;
        }
        const uint8_t bit = static_cast<uint8_t>(1U << item.channel_id);
        if ((seen & bit) != 0U) return false;
        seen |= bit;
    }
    if (seen != static_cast<uint8_t>((1U << KUGLASS_CHANNEL_COUNT) - 1U)) {
        return false;
    }
    for (size_t i = 0; i < command.channel_count; ++i) {
        const ProtocolChannelCommand& item = command.channels[i];
        ChannelRuntime& target = channels_[item.channel_id];
        target.target_mi = clamp_mi(item.mi);
        target.enable = item.enable;
    }
    return true;
}

void ChannelManager::update(float dt_s, bool global_enable) {
    if (!std::isfinite(dt_s) || dt_s < 0.0f) dt_s = 0.0f;
    for (ChannelRuntime& item : channels_) {
        if (!global_enable || !item.enable || item.faulted) {
            // Safe-off is a hard electrical stop, not a cosmetic ramp.  This
            // also keeps reported applied_mi equal to the actual output.
            item.applied_mi = 0.0f;
            continue;
        }
        const float desired = item.target_mi;
        if (desired < item.applied_mi) {
            item.applied_mi -= KUGLASS_MI_ATTACK_PER_S * dt_s;
            if (item.applied_mi < desired) item.applied_mi = desired;
        } else {
            item.applied_mi += KUGLASS_MI_RELEASE_PER_S * dt_s;
            if (item.applied_mi > desired) item.applied_mi = desired;
        }
        item.applied_mi = clamp_mi(item.applied_mi);
    }
}

void ChannelManager::set_fault(size_t index, bool faulted) {
    if (index < KUGLASS_CHANNEL_COUNT) channels_[index].faulted = faulted;
}

void ChannelManager::clear_faults() {
    for (ChannelRuntime& item : channels_) item.faulted = false;
}

ChannelRuntime* ChannelManager::channel(size_t index) {
    return index < KUGLASS_CHANNEL_COUNT ? &channels_[index] : nullptr;
}

const ChannelRuntime* ChannelManager::channel(size_t index) const {
    return index < KUGLASS_CHANNEL_COUNT ? &channels_[index] : nullptr;
}
