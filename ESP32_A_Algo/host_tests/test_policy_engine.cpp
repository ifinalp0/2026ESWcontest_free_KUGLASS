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

SensorSnapshot moderate_camera(uint32_t now_ms, float saturation) {
    SensorSnapshot sensors;
    sensors.camera_valid = true;
    sensors.camera_timestamp_ms = now_ms;
    sensors.front_left.saturation_ratio = saturation;
    sensors.front_left.highlight_area = 0.2f;
    sensors.front_left.edge_density = 0.18f;
    sensors.front_right = sensors.front_left;
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
    // Display-left is the driver side, so both driver-side channels must use
    // the left ROI rather than the old shared max(left, right) value.
    assert(glare.channels[2].target_mi < glare.channels[3].target_mi);
    assert(glare.channels[0].target_transmission >= 0.45f);
    for (const PolicyChannelTarget& channel : glare.channels) {
        assert(channel.target_mi <= KUGLASS_MAX_MODULATION_INDEX);
        assert(channel.applied_mi <= KUGLASS_MAX_MODULATION_INDEX);
    }

    PolicyEngine passenger_glare_engine;
    passenger_glare_engine.begin();
    SensorSnapshot passenger_glare = fresh_camera(now);
    passenger_glare.front_right = passenger_glare.front_left;
    passenger_glare.front_left = CameraRoiMetrics{};
    passenger_glare.front_left.edge_density = 0.18f;
    PolicyDecision passenger = passenger_glare_engine.update(passenger_glare, now);
    assert(passenger.glare_right > passenger.glare_left);
    assert(passenger.channels[1].target_mi < passenger.channels[0].target_mi);
    assert(passenger.channels[3].target_mi < passenger.channels[2].target_mi);

    PolicyEngine thermal_engine;
    thermal_engine.begin();
    SensorSnapshot hot;
    hot.internal_temp_valid = true;
    hot.internal_temp_c = 42.0f;
    hot.internal_temp_timestamp_ms = now;
    PolicyDecision thermal = thermal_engine.update(hot, now);
    assert(thermal.thermal_risk > 0.99f);
    assert(thermal.channels[3].target_mi < thermal.channels[0].target_mi);

    PolicyEngine thermal_demo_engine;
    thermal_demo_engine.begin();
    assert(thermal_demo_engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":1,\"command\":\"set_demo\","
        "\"demo_mode\":\"hot_summer\"}"), now).accepted);
    SensorSnapshot cool_demo;
    cool_demo.internal_temp_valid = true;
    cool_demo.internal_temp_c = 25.0f;
    cool_demo.internal_temp_timestamp_ms = now;
    const PolicyDecision cool_thermal_demo =
        thermal_demo_engine.update(cool_demo, now);
    assert(cool_thermal_demo.thermal_risk == 0.0f);
    SensorSnapshot hot_demo = cool_demo;
    hot_demo.internal_temp_c = 42.0f;
    hot_demo.internal_temp_timestamp_ms = now + 50U;
    const PolicyDecision hot_thermal_demo =
        thermal_demo_engine.update(hot_demo, now + 50U);
    assert(hot_thermal_demo.thermal_risk > 0.99f);
    assert(hot_thermal_demo.channels[2].target_mi <
           cool_thermal_demo.channels[2].target_mi);

    PolicyEngine privacy_engine;
    privacy_engine.begin();
    assert(privacy_engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":1,\"command\":\"set_mode\","
        "\"mode\":\"camping\"}"), now).accepted);
    PolicyDecision privacy = privacy_engine.update(SensorSnapshot{}, now);
    for (const PolicyChannelTarget& channel : privacy.channels) {
        assert(channel.target_mi < 0.12f);
        assert(channel.target_mi > 0.0f);
        assert(channel.optical_state == OpticalState::FROST);
        assert(channel.enable);
    }
    ProtocolCommand privacy_command;
    assert(policy_decision_to_protocol(privacy, 250, &privacy_command));
    for (const ProtocolChannelCommand& channel : privacy_command.channels) {
        assert(channel.mi > 0.0f);
        assert(channel.enable);
    }

    PolicyEngine parked_engine;
    parked_engine.begin();
    assert(parked_engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":1,\"command\":\"set_mode\","
        "\"mode\":\"parked\"}"), now).accepted);
    const PolicyDecision parked = parked_engine.update(SensorSnapshot{}, now);
    for (const PolicyChannelTarget& channel : parked.channels) {
        assert(channel.target_mi > 0.0f);
        assert(channel.target_mi < 0.12f);
        assert(channel.optical_state == OpticalState::FROST);
        assert(channel.enable);
    }

    PolicyEngine noise_engine;
    noise_engine.begin();
    const PolicyDecision noise_baseline =
        noise_engine.update(moderate_camera(now, 0.50f), now);
    const PolicyDecision small_camera_change =
        noise_engine.update(moderate_camera(now + 50U, 0.52f), now + 50U);
    const float small_target_delta = std::fabs(
        small_camera_change.channels[0].target_mi -
        noise_baseline.channels[0].target_mi);
    assert(small_target_delta > 0.0f);
    assert(small_target_delta < KUGLASS_MI_AUTO_DEADBAND);
    assert(std::fabs(small_camera_change.channels[0].applied_mi -
                     noise_baseline.channels[0].applied_mi) < 0.00001f);

    const PolicyDecision material_camera_change =
        noise_engine.update(moderate_camera(now + 100U, 0.70f), now + 100U);
    assert(std::fabs(material_camera_change.channels[0].applied_mi -
                     noise_baseline.channels[0].applied_mi) >
           KUGLASS_MI_AUTO_DEADBAND);

    PolicyEngine response_engine;
    response_engine.begin();
    const PolicyDecision clear = response_engine.update(SensorSnapshot{}, now);
    assert(std::fabs(clear.channels[0].target_transmission - 1.0f) < 0.0001f);
    assert(std::fabs(clear.channels[0].target_mi -
                     KUGLASS_MAX_MODULATION_INDEX) < 0.0001f);
    assert(response_engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":2,\"command\":\"set_mode\","
        "\"mode\":\"camping\"}"), now + 1U).accepted);
    PolicyDecision fast_response;
    for (uint32_t elapsed = 50U; elapsed <= 250U; elapsed += 50U) {
        fast_response = response_engine.update(SensorSnapshot{}, now + elapsed);
    }
    assert(clear.channels[0].applied_mi - fast_response.channels[0].applied_mi >
           0.47f);
    assert(std::fabs(fast_response.channels[0].applied_mi -
                     fast_response.channels[0].target_mi) < 0.01f);

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

    UiCommand persistent_manual = parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":21,\"command\":\"manual_channel\","
        "\"channel_id\":3,\"target_mi\":0.3,\"ttl_ms\":0,\"enable\":true}");
    assert(manual_engine.apply_command(persistent_manual, now + 1200).accepted);
    const PolicyDecision persistent_decision =
        manual_engine.update(SensorSnapshot{}, now + 1000000U);
    assert(persistent_decision.channels[3].manual);
    assert(persistent_decision.channels[3].manual_persistent);

    UiCommand return_auto = parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":22,\"command\":\"return_auto\","
        "\"channel_id\":3}");
    assert(manual_engine.apply_command(return_auto, now + 1000001U).accepted);
    assert(!manual_engine.update(SensorSnapshot{}, now + 1000050U).channels[3].manual);

    UiCommand diagnostic = parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":23,\"command\":\"set_environment\","
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
