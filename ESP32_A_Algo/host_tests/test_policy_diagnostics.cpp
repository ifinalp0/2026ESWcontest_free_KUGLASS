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

SensorSnapshot physical_temperature(float temperature_c, uint32_t now_ms) {
    SensorSnapshot sensors;
    sensors.internal_temp_valid = true;
    sensors.internal_temp_c = temperature_c;
    sensors.internal_temp_timestamp_ms = now_ms;
    return sensors;
}

}  // namespace

int main() {
    static_assert(KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS == 1);
    constexpr uint32_t now = 10000U;

    PolicyEngine engine;
    engine.begin();
    assert(engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":1,\"command\":\"set_demo\","
        "\"demo_mode\":\"hot_summer\"}"), now).accepted);

    const SensorSnapshot physical = physical_temperature(30.0f, now);
    assert(engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":2,\"command\":\"set_environment\","
        "\"environment\":{\"internal_temp_c\":45.0}}"), now + 1U).accepted);
    SensorSnapshot effective = engine.effective_sensors(physical, now + 2U);
    assert(effective.internal_temp_valid);
    assert(std::fabs(effective.internal_temp_c - 45.0f) < 0.0001f);
    const PolicyDecision injected = engine.update(physical, now + 2U);
    assert(injected.thermal_risk > 0.99f);
    assert(injected.temperature_override_active);

    // JSON null releases only the diagnostic override and restores the
    // physical DS18B20 value as the policy input.
    assert(engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":3,\"command\":\"set_environment\","
        "\"environment\":{\"internal_temp_c\":null}}"), now + 3U).accepted);
    effective = engine.effective_sensors(physical, now + 4U);
    assert(effective.internal_temp_valid);
    assert(std::fabs(effective.internal_temp_c - 30.0f) < 0.0001f);
    const PolicyDecision restored = engine.update(physical, now + 4U);
    assert(restored.thermal_risk > 0.19f && restored.thermal_risk < 0.21f);
    assert(!restored.temperature_override_active);

    // Selecting any scenario also starts from physical inputs.
    assert(engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":4,\"command\":\"set_environment\","
        "\"environment\":{\"internal_temp_c\":50.0}}"), now + 5U).accepted);
    assert(engine.apply_command(parse(
        "{\"v\":1,\"type\":\"ui_command\",\"seq\":5,\"command\":\"set_demo\","
        "\"demo_mode\":\"none\"}"), now + 6U).accepted);
    effective = engine.effective_sensors(physical, now + 7U);
    assert(std::fabs(effective.internal_temp_c - 30.0f) < 0.0001f);

    std::puts("policy diagnostics ok");
    return 0;
}
