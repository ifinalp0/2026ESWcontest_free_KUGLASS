#include "policy_engine.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

UiCommand parse(const char* line) {
    UiCommand command;
    UiProtocolError error;
    assert(parse_ui_command_line(line, &command, &error));
    return command;
}

SensorSnapshot fresh_camera(uint32_t now_ms) {
    SensorSnapshot sensors;
    sensors.camera_valid = true;
    sensors.camera_timestamp_ms = now_ms;
    sensors.front_left.saturation_ratio = 1.0f;
    sensors.front_left.highlight_area = 1.0f;
    sensors.front_left.edge_density = 0.02f;
    sensors.front_right.edge_density = 0.18f;
    return sensors;
}

}  // namespace

int main() {
    constexpr uint32_t now = 10000;

    PolicyEngine glare_engine;
    glare_engine.begin();
    PolicyDecision glare = glare_engine.update(fresh_camera(now), now);
    assert(glare.strong_front_light);
    assert(glare.glare_left > glare.glare_right);
    assert(glare.channels[0].target_mi < glare.channels[1].target_mi);
    assert(glare.channels[0].target_transmission >= 0.45f);

    PolicyEngine thermal_engine;
    thermal_engine.begin();
    SensorSnapshot hot;
    hot.internal_temp_valid = true;
    hot.internal_temp_c = 42.0f;
    hot.internal_temp_timestamp_ms = now;
    PolicyDecision thermal = thermal_engine.update(hot, now);
    assert(thermal.thermal_risk > 0.99f);
    assert(thermal.channels[3].target_mi < thermal.channels[0].target_mi);

    PolicyEngine privacy_engine;
    privacy_engine.begin();
    assert(privacy_engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":1,\"command\":\"set_mode\","
        "\"mode\":\"camping\"}"), now).accepted);
    PolicyDecision privacy = privacy_engine.update(SensorSnapshot{}, now);
    for (const PolicyChannelTarget& channel : privacy.channels) {
        assert(channel.target_mi < 0.12f);
        assert(channel.enable);
    }

    PolicyEngine manual_engine;
    manual_engine.begin();
    manual_engine.update(SensorSnapshot{}, now);
    UiCommand manual = parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":20,\"command\":\"manual_channel\","
        "\"channel_id\":3,\"target_mi\":0.2,\"ttl_ms\":1000,\"enable\":true}");
    assert(manual_engine.apply_command(manual, now + 10).accepted);
    assert(manual_engine.update(SensorSnapshot{}, now + 50).channels[3].manual);
    assert(!manual_engine.update(SensorSnapshot{}, now + 1100).channels[3].manual);

    PolicyApplyResult duplicate = manual_engine.apply_command(manual, now + 1200);
    assert(!duplicate.accepted);

    UiCommand diagnostic = parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":21,\"command\":\"set_environment\","
        "\"environment\":{\"internal_temp_c\":40}}");
    assert(!manual_engine.apply_command(diagnostic, now + 1200).accepted);

    ProtocolCommand downstream;
    assert(policy_decision_to_protocol(glare, 250, &downstream));
    assert(downstream.channel_count == 4);
    char line[384];
    assert(format_command_line(downstream, line, sizeof(line)));
    ProtocolCommand round_trip;
    ProtocolError protocol_error;
    assert(parse_command_line(line, &round_trip, &protocol_error));
    assert(round_trip.channel_count == 4);

    std::puts("policy engine ok");
    return 0;
}
