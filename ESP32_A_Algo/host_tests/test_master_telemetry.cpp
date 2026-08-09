#include "master_telemetry.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

size_t count_occurrences(const char* text, const char* needle) {
    size_t count = 0;
    const size_t length = std::strlen(needle);
    for (const char* p = text; (p = std::strstr(p, needle)) != nullptr; p += length) {
        ++count;
    }
    return count;
}

}  // namespace

int main() {
    PolicyDecision decision;
    decision.seq = 41;
    decision.vehicle_mode = VehicleMode::STOPPED;
    decision.demo_mode = DemoMode::CAMERA_SATURATION;
    decision.thermal_risk = 0.42f;
    decision.glare_total = 0.73f;
    decision.decision_reason = "camera glare: directional fast response";
    for (size_t i = 0; i < KUGLASS_TOTAL_CHANNELS; ++i) {
        PolicyChannelTarget& channel = decision.channels[i];
        channel.channel_id = static_cast<uint8_t>(i);
        channel.target_mi = 0.1f * static_cast<float>(i);
        channel.applied_mi = channel.target_mi;
        channel.target_transmission = channel.target_mi;
        channel.enable = true;
        channel.optical_state = i < 3 ? OpticalState::FROST : OpticalState::CLEAR;
        channel.manual = i == 0;
        channel.manual_persistent = i == 0;
    }

    SensorSnapshot sensors;
    sensors.camera_valid = true;
    sensors.camera_frame_id = 12;
    sensors.camera_timestamp_ms = 12000;
    sensors.front_left.saturation_ratio = 0.73f;
    sensors.front_right.saturation_ratio = 0.21f;
    sensors.internal_temp_valid = true;
    sensors.internal_temp_c = 31.25f;

    char line[4096];
    assert(format_master_state(
        decision, sensors, 12500, true, "NONE", line, sizeof(line)));
    assert(line[0] == '{');
    assert(line[std::strlen(line) - 1] == '}');
    assert(std::strstr(line, "\"type\":\"state\"") != nullptr);
    assert(std::strstr(line, "\"vehicle_mode\":\"stopped\"") != nullptr);
    assert(std::strstr(line, "\"camera_metrics\"") != nullptr);
    assert(std::strstr(line, "\"channels\"") != nullptr);
    assert(std::strstr(line, "\"decision_reason\"") != nullptr);
    assert(std::strstr(line, "\"downstream\"") != nullptr);
    assert(std::strstr(line, "\"applied_mi\"") == nullptr);
    assert(std::strstr(line, "\"manual_active\":true") != nullptr);
    assert(std::strstr(line, "\"manual_persistent\":true") != nullptr);
    assert(count_occurrences(line, "\"channel_id\":") == KUGLASS_TOTAL_CHANNELS);

    // stdout intentionally contains exactly one JSON document; run_tests.sh
    // additionally feeds it to JSON.parse for syntax validation.
    std::puts(line);
    return 0;
}
