#include "analog_monitor.h"
#include "channel_manager.h"
#include "control_protocol.h"
#include "estop_input_qualifier.h"
#include "fault_manager.h"
#include "fault_input_qualifier.h"
#include "json_line_accumulator.h"
#include "kuglass_b_config.h"
#include "power_stage_pinmap.h"
#include "protocol.h"
#include "spwm_generator.h"
#include "status_reporter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

bool near(float left, float right, float tolerance = 0.00001f) {
    return std::fabs(left - right) <= tolerance;
}

ProtocolCommand make_full_command(float mi, bool enable = true) {
    ProtocolCommand command;
    command.seq = 1;
    command.ttl_ms = 250;
    command.channel_count = KUGLASS_CHANNEL_COUNT;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        command.channels[i].channel_id = static_cast<uint8_t>(i);
        command.channels[i].mi = mi;
        command.channels[i].enable = enable;
    }
    return command;
}

struct EnableCommitProbe {
    size_t calls = 0;
    bool allow = false;
};

bool probe_enable_commit(size_t channel_index, void* context) {
    assert(channel_index < KUGLASS_CHANNEL_COUNT);
    auto* probe = static_cast<EnableCommitProbe*>(context);
    assert(probe != nullptr);
    ++probe->calls;
    return probe->allow;
}

}  // namespace

int main() {
    EstopInputQualifier estop_qualifier;
    assert(estop_qualifier.sample(true) ==
           EstopInputQualification::HEALTHY);
    estop_qualifier.note_falling_edge();
    for (uint8_t i = 1; i < KUGLASS_ESTOP_CONFIRM_SAMPLES; ++i) {
        assert(estop_qualifier.sample(false) ==
               EstopInputQualification::QUALIFYING_LOW);
    }
    assert(estop_qualifier.consecutive_low_samples() ==
           KUGLASS_ESTOP_CONFIRM_SAMPLES - 1U);
    assert(estop_qualifier.sample(false) ==
           EstopInputQualification::CONFIRMED_LOW);
    assert(estop_qualifier.sample(true) ==
           EstopInputQualification::CONFIRMED_LOW);
    assert(estop_qualifier.confirmed());

    estop_qualifier.reset();
    estop_qualifier.note_falling_edge();
    for (uint8_t i = 1; i < KUGLASS_ESTOP_CONFIRM_SAMPLES; ++i) {
        assert(estop_qualifier.sample(false) ==
               EstopInputQualification::QUALIFYING_LOW);
    }
    for (uint8_t i = 1; i < KUGLASS_ESTOP_RELEASE_SAMPLES; ++i) {
        assert(estop_qualifier.sample(true) ==
               EstopInputQualification::QUALIFYING_HIGH);
    }
    assert(estop_qualifier.consecutive_high_samples() ==
           KUGLASS_ESTOP_RELEASE_SAMPLES - 1U);
    assert(estop_qualifier.sample(true) ==
           EstopInputQualification::RECOVERED_HIGH);
    assert(!estop_qualifier.pending());
    assert(estop_qualifier.sample(true) ==
           EstopInputQualification::HEALTHY);

    estop_qualifier.note_falling_edge();
    for (uint8_t i = 1; i < KUGLASS_ESTOP_RELEASE_SAMPLES; ++i) {
        assert(estop_qualifier.sample(true) ==
               EstopInputQualification::QUALIFYING_HIGH);
    }
    estop_qualifier.note_falling_edge();
    assert(estop_qualifier.consecutive_high_samples() == 0U);
    assert(estop_qualifier.sample(false) ==
           EstopInputQualification::QUALIFYING_LOW);
    assert(estop_qualifier.sample(true) ==
           EstopInputQualification::QUALIFYING_HIGH);

    FaultInputQualifier fault_qualifier;
    assert(fault_qualifier.sample(true) ==
           FaultInputQualification::HEALTHY);
    assert(fault_qualifier.sample(false) ==
           FaultInputQualification::QUALIFYING_LOW);
    assert(fault_qualifier.sample(false) ==
           FaultInputQualification::QUALIFYING_LOW);
    assert(fault_qualifier.consecutive_low_samples() == 2U);
    assert(fault_qualifier.sample(true) ==
           FaultInputQualification::HEALTHY);
    for (uint8_t burst = 0; burst < 3U; ++burst) {
        for (uint8_t i = 1; i < KUGLASS_FAULT_CONFIRM_SAMPLES; ++i) {
            assert(fault_qualifier.sample(false) ==
                   FaultInputQualification::QUALIFYING_LOW);
        }
        assert(fault_qualifier.sample(true) ==
               FaultInputQualification::HEALTHY);
    }
    for (uint8_t i = 1; i < KUGLASS_FAULT_CONFIRM_SAMPLES; ++i) {
        assert(fault_qualifier.sample(false) ==
               FaultInputQualification::QUALIFYING_LOW);
    }
    assert(fault_qualifier.sample(false) ==
           FaultInputQualification::CONFIRMED_LOW);
    assert(fault_qualifier.sample(false) ==
           FaultInputQualification::CONFIRMED_LOW);
    fault_qualifier.reset();

    const PowerStagePinmap& pinmap = KUGLASS_POWER_STAGE_PINMAP;
    assert(pinmap.estop_n_gpio == 19);

    const int expected[4][6] = {
        {10, 11, 12, 13, 1, 2},
        {14, 15, 16, 17, 4, 5},
        {18, 21, 38, 39, 6, 7},
        {40, 41, 42, 47, 8, 3},
    };
    const uint8_t expected_adc_channels[4][2] = {
        {0, 1}, {3, 4}, {5, 6}, {7, 2},
    };
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const PowerStageChannelPins& channel = pinmap.channels[i];
        assert(channel.pwm_gpio == expected[i][0]);
        assert(channel.direction_gpio == expected[i][1]);
        assert(channel.enable_gpio == expected[i][2]);
        assert(channel.fault_n_gpio == expected[i][3]);
        assert(channel.current_adc_gpio == expected[i][4]);
        assert(channel.temperature_adc_gpio == expected[i][5]);
        assert(KUGLASS_ANALOG_INPUTS[i * 2].adc1_channel ==
               expected_adc_channels[i][0]);
        assert(KUGLASS_ANALOG_INPUTS[i * 2 + 1].adc1_channel ==
               expected_adc_channels[i][1]);
    }
    assert(kuglass_power_stage_pinmap_is_unique());
    assert(AnalogMonitor::pinmap_matches_logic_carrier(pinmap));
    PowerStagePinmap wrong_pinmap = pinmap;
    wrong_pinmap.channels[2].current_adc_gpio = 9;
    assert(!AnalogMonitor::pinmap_matches_logic_carrier(wrong_pinmap));
    assert(KUGLASS_A_UART_TX_GPIO == 43);
    assert(KUGLASS_A_UART_RX_GPIO == 44);
    assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_TX_GPIO));
    assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_RX_GPIO));
    assert(near(KUGLASS_MAX_MODULATION_INDEX, 0.95f));

    ProtocolCommand command;
    ProtocolError error;
    assert(parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":7,"
        "\"ttl_ms\":250,\"ch\":[[0,0.2,true],[1,0.4,true],"
        "[2,0.6,true],[3,0.8,true]]}",
        &command, &error));
    assert(!parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":8,"
        "\"ttl_ms\":250,\"ch\":[[0,0.2,true],[0,0.4,true],"
        "[2,0.6,true],[3,0.8,true]]}",
        &command, &error));
    assert(error == ProtocolError::BAD_CHANNEL_ARRAY);
    assert(!parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":8,"
        "\"ttl_ms\":49,\"ch\":[[0,0.2,true],[1,0.4,true],"
        "[2,0.6,true],[3,0.8,true]]}",
        &command, &error));
    assert(error == ProtocolError::BAD_TTL);

    ResetFaultCommand reset;
    ControlProtocolError control_error;
    assert(parse_reset_fault_line(
        "{\"v\":1,\"type\":\"control\",\"seq\":42,"
        "\"source_session_id\":1001,\"target_boot_id\":2002,"
        "\"reset_challenge\":3003,"
        "\"command\":\"reset_fault\"}",
        &reset, &control_error));
    assert(reset.seq == 42);
    assert(reset.source_session_id == 1001);
    assert(reset.target_boot_id == 2002);
    assert(reset.reset_challenge == 3003);
    assert(parse_reset_fault_line(
        "{\"extra\":{\"nested\":[1,true,null]},\"v\":1,"
        "\"command\":\"reset_fault\",\"seq\":43,"
        "\"reset_challenge\":3004,\"target_boot_id\":2002,"
        "\"source_session_id\":1001,"
        "\"type\":\"control\"}",
        &reset, &control_error));
    assert(reset.seq == 43);
    assert(!parse_reset_fault_line(
        "{\"v\":2,\"type\":\"control\",\"seq\":44,"
        "\"source_session_id\":1001,\"target_boot_id\":2002,"
        "\"reset_challenge\":3003,"
        "\"command\":\"reset_fault\"}",
        &reset, &control_error));
    assert(control_error == ControlProtocolError::BAD_VERSION);
    assert(!parse_reset_fault_line(
        "{\"v\":1,\"type\":\"control\",\"seq\":44,\"seq\":45,"
        "\"source_session_id\":1001,\"target_boot_id\":2002,"
        "\"reset_challenge\":3003,"
        "\"command\":\"reset_fault\"}",
        &reset, &control_error));
    assert(control_error == ControlProtocolError::DUPLICATE_FIELD);
    assert(!parse_reset_fault_line(
        "{\"v\":1,\"type\":\"control\",\"seq\":44,"
        "\"source_session_id\":1001,\"target_boot_id\":2002,"
        "\"command\":\"reset_fault\"}",
        &reset, &control_error));
    assert(control_error == ControlProtocolError::MISSING_RESET_CHALLENGE);
    assert(!parse_reset_fault_line(
        "{\"v\":1,\"type\":\"control\",\"seq\":44,"
        "\"source_session_id\":0,\"target_boot_id\":2002,"
        "\"reset_challenge\":3003,\"command\":\"reset_fault\"}",
        &reset, &control_error));
    assert(control_error == ControlProtocolError::BAD_SOURCE_SESSION_ID);

    assert(parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":7,"
        "\"ttl_ms\":250,\"ch\":[[0,0.2,true],[1,0.4,true],"
        "[2,0.6,true],[3,0.8,true]]}",
        &command, &error));
    ChannelManager channels;
    channels.begin();
    assert(channels.apply_command(command));
    channels.update(1.0f, true);
    assert(channels.count() == 4);
    assert(near(channels.channel(3)->applied_mi, 0.7f));

    ProtocolCommand maximum = make_full_command(1.0f);
    assert(channels.apply_command(maximum));
    assert(near(channels.channel(0)->target_mi,
                KUGLASS_MAX_MODULATION_INDEX));
    ProtocolCommand duplicate = maximum;
    duplicate.channels[0].mi = 0.1f;
    duplicate.channels[1].channel_id = 0;
    assert(!channels.apply_command(duplicate));
    assert(near(channels.channel(0)->target_mi,
                KUGLASS_MAX_MODULATION_INDEX));

    channels.update(2.0f, true);
    for (size_t i = 0; i < channels.count(); ++i) {
        assert(near(channels.channel(i)->applied_mi,
                    KUGLASS_MAX_MODULATION_INDEX));
    }
    channels.update(0.0f, false);
    for (size_t i = 0; i < channels.count(); ++i) {
        assert(near(channels.channel(i)->applied_mi, 0.0f));
    }
    channels.update(0.001f, true);
    assert(near(channels.channel(0)->applied_mi, 0.0007f));
    channels.set_fault(0, true);
    channels.update(0.0f, true);
    assert(near(channels.channel(0)->applied_mi, 0.0f));
    for (size_t i = 1; i < channels.count(); ++i) {
        assert(near(channels.channel(i)->applied_mi, 0.0007f));
        assert(!channels.channel(i)->faulted);
    }
    channels.clear_faults();

    FaultManager fault;
    fault.begin();
    fault.note_command(100, 250);
    fault.update(200, true);
    assert(!fault.faulted());
    fault.update(351, true);
    assert(fault.code() == FaultCode::COMM_TIMEOUT);
    fault.reject_command();
    assert(fault.code() == FaultCode::INVALID_COMMAND);
    fault.note_command(400, 250);
    assert(!fault.faulted());
    fault.update(450, false);
    assert(fault.code() == FaultCode::ESTOP);
    fault.update(451, true);
    assert(fault.code() == FaultCode::ESTOP);
    assert(!fault.clear_if_safe(false));
    assert(fault.clear_if_safe(true));

    AnalogMedianEwmaFilter filter;
    int filtered = 0;
    const int first_samples[5] = {100, 400, 200, 0, 300};
    assert(filter.update(first_samples, 5, &filtered));
    assert(filtered == 200);
    const int second_samples[5] = {280, 280, 280, 280, 280};
    assert(filter.update(second_samples, 5, &filtered));
    assert(filtered == 210);
    const int invalid_samples[5] = {0, 1, 2, 3, 4096};
    assert(!filter.update(invalid_samples, 5, &filtered));
    assert(filter.value() == 210);
    assert(analog_current_valid_bit(0) == 0x01);
    assert(analog_temperature_valid_bit(3) == 0x80);
    const uint32_t before_wrap = std::numeric_limits<uint32_t>::max() - 5U;
    assert(analog_elapsed_ms(3U, before_wrap) == 9U);
    assert(analog_timestamp_is_fresh(true, 3U, before_wrap, 9U));
    assert(!analog_timestamp_is_fresh(true, 3U, before_wrap, 8U));

    AnalogTelemetrySnapshot analog;
    analog.initialized = true;
    analog.current_calibrated = true;
    analog.temperature_calibrated = true;
    analog.raw_valid_mask = 0xFF;
    analog.mv_valid_mask = 0xFF;
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        analog.current_raw[i] = 100 + static_cast<int>(i);
        analog.temperature_raw[i] = 200 + static_cast<int>(i);
        analog.current_mv[i] = 10 + static_cast<int>(i);
        analog.temperature_mv[i] = 1000 + static_cast<int>(i);
    }
    char status[1024];
    const ResetControlResult reset_result = {
        42, 1001, true, "NONE",
    };
    const StatusMetadata result_metadata = {
        2002, 3004, &reset_result,
    };
    assert(format_status_line(123, channels, fault, false, result_metadata,
                              &analog, "RESET_OK", status, sizeof(status)));
    assert(std::strstr(status, "\"controller_id\":\"B\"") != nullptr);
    assert(std::strstr(status, "\"seq\":123") != nullptr);
    assert(std::strstr(status, "\"boot_id\":2002") != nullptr);
    assert(std::strstr(status, "\"reset_challenge\":3004") != nullptr);
    assert(std::strstr(status, "\"diagnostic\":\"RESET_OK\"") != nullptr);
    assert(std::strstr(status,
        "\"control_result\":{\"command\":\"reset_fault\","
        "\"seq\":42,\"source_session_id\":1001,\"ok\":true,"
        "\"error\":\"NONE\"}") != nullptr);
    assert(std::strstr(status, "\"raw_valid_mask\":255") != nullptr);
    assert(std::strstr(status, "\"id\":3") != nullptr);
    assert(std::strstr(status, "\"id\":4") == nullptr);
    channels.set_fault(2, true);
    assert(format_status_line(124, channels, fault, false, result_metadata,
                              status, sizeof(status)));
    assert(std::strstr(status,
        "\"id\":1,\"mi\":0.0007,\"fault\":false") != nullptr);
    assert(std::strstr(status,
        "\"id\":2,\"mi\":0.0007,\"fault\":true") != nullptr);
    assert(std::strstr(status, "\"fault_code\":\"NONE\"") != nullptr);
    channels.clear_faults();
    assert(!format_status_line(124, channels, fault, false, result_metadata,
                               &analog, "bad-value", status, sizeof(status)));
    char small_status[32];
    assert(!format_status_line(124, channels, fault, false, result_metadata,
                               &analog, small_status, sizeof(small_status)));
    const StatusMetadata regular_metadata = {2002, 3004, nullptr};
    assert(format_status_line(125, channels, fault, false, regular_metadata,
                              status, sizeof(status)));
    assert(std::strstr(status, "\"adc\"") == nullptr);
    const StatusMetadata invalid_metadata = {0, 3004, nullptr};
    assert(!format_status_line(126, channels, fault, false, invalid_metadata,
                               status, sizeof(status)));

    ChannelManager isolated_fault_channels;
    isolated_fault_channels.begin();
    assert(isolated_fault_channels.apply_command(maximum));
    isolated_fault_channels.update(2.0f, true);
    isolated_fault_channels.set_fault(2, true);
    isolated_fault_channels.update(0.0f, true);
    SpwmGenerator isolated_fault_waveform;
    isolated_fault_waveform.begin(pinmap);
    isolated_fault_waveform.tick(
        isolated_fault_channels, 1.0f / 240.0f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(isolated_fault_waveform.state(i)->enabled == (i != 2U));
    }

    ChannelManager waveform_channels;
    waveform_channels.begin();
    assert(waveform_channels.apply_command(maximum));
    waveform_channels.update(2.0f, true);
    SpwmGenerator waveform;
    waveform.begin(pinmap);
    waveform.tick(waveform_channels, 0.008f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const SpwmChannelState* state = waveform.state(i);
        assert(state != nullptr && state->enabled);
        assert(state->direction_positive);
        assert(state->duty_ratio > 0.0f);
        assert(state->duty_ratio <= KUGLASS_MAX_MODULATION_INDEX);
    }
    waveform.tick(waveform_channels, 0.001f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const SpwmChannelState* state = waveform.state(i);
        assert(state != nullptr && state->enabled && state->blanking);
        assert(state->direction_positive);
        assert(near(state->duty_ratio, 0.0f));
    }
    waveform.tick(waveform_channels, 0.001f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const SpwmChannelState* state = waveform.state(i);
        assert(state != nullptr && state->enabled && !state->blanking);
        assert(!state->direction_positive);
    }
    waveform.force_channel_safe(2);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(waveform.state(i)->enabled == (i != 2U));
    }
    waveform.tick(waveform_channels, 0.0f, true);
    assert(waveform.state(2)->enabled);
    waveform.force_safe();
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const SpwmChannelState* state = waveform.state(i);
        assert(state != nullptr && !state->enabled && !state->blanking);
        assert(near(state->duty_ratio, 0.0f));
    }

    ChannelManager sub_tick_channels;
    sub_tick_channels.begin();
    assert(sub_tick_channels.apply_command(make_full_command(0.001f)));
    sub_tick_channels.update(2.0f, true);
    SpwmGenerator sub_tick_waveform;
    sub_tick_waveform.begin(pinmap);
    sub_tick_waveform.tick(sub_tick_channels, 1.0f / 240.0f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(sub_tick_waveform.state(i)->enabled);
        assert(near(sub_tick_waveform.state(i)->duty_ratio, 0.0f));
    }

    SpwmGenerator zero_crossing_waveform;
    zero_crossing_waveform.begin(pinmap);
    zero_crossing_waveform.tick(waveform_channels, 0.0f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(zero_crossing_waveform.state(i)->enabled);
        assert(near(zero_crossing_waveform.state(i)->duty_ratio, 0.0f));
    }

    ChannelManager disabled_channels;
    disabled_channels.begin();
    assert(disabled_channels.apply_command(make_full_command(0.5f, false)));
    disabled_channels.update(1.0f, true);
    SpwmGenerator disabled_waveform;
    disabled_waveform.begin(pinmap);
    disabled_waveform.tick(disabled_channels, 1.0f / 240.0f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(!disabled_waveform.state(i)->enabled);
    }

    ChannelManager zero_mi_channels;
    zero_mi_channels.begin();
    assert(zero_mi_channels.apply_command(make_full_command(0.0f)));
    zero_mi_channels.update(1.0f, true);
    SpwmGenerator zero_mi_waveform;
    zero_mi_waveform.begin(pinmap);
    zero_mi_waveform.tick(zero_mi_channels, 1.0f / 240.0f, true);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(!zero_mi_waveform.state(i)->enabled);
    }

    EnableCommitProbe allowed;
    allowed.allow = true;
    SpwmGenerator static_enable_waveform;
    static_enable_waveform.begin(pinmap);
    static_enable_waveform.set_enable_commit_callback(
        probe_enable_commit, &allowed);
    static_enable_waveform.tick(waveform_channels, 0.008f, true);
    assert(allowed.calls == KUGLASS_CHANNEL_COUNT);
    static_enable_waveform.tick(waveform_channels, 0.001f, true);
    assert(allowed.calls == KUGLASS_CHANNEL_COUNT);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(static_enable_waveform.state(i)->enabled);
        assert(static_enable_waveform.state(i)->blanking);
    }
    static_enable_waveform.tick(waveform_channels, 0.001f, true);
    assert(allowed.calls == KUGLASS_CHANNEL_COUNT * 2U);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(static_enable_waveform.state(i)->enabled);
        assert(!static_enable_waveform.state(i)->blanking);
    }
    static_enable_waveform.tick(waveform_channels, 0.001f, false);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(!static_enable_waveform.state(i)->enabled);
    }

    EnableCommitProbe denied;
    SpwmGenerator guarded_waveform;
    guarded_waveform.begin(pinmap);
    guarded_waveform.set_enable_commit_callback(probe_enable_commit, &denied);
    guarded_waveform.tick(waveform_channels, 1.0f / 240.0f, true);
    assert(denied.calls == KUGLASS_CHANNEL_COUNT);
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        assert(!guarded_waveform.state(i)->enabled);
    }

    JsonLineAccumulator accumulator;
    char json_line[16];
    assert(accumulator.feed('{', json_line, sizeof(json_line)) ==
           JsonLineResult::NONE);
    assert(accumulator.feed('}', json_line, sizeof(json_line)) ==
           JsonLineResult::NONE);
    assert(accumulator.feed('\n', json_line, sizeof(json_line)) ==
           JsonLineResult::COMPLETE);
    assert(std::strcmp(json_line, "{}") == 0);
    for (size_t i = 0; i < JsonLineAccumulator::kCapacity; ++i) {
        assert(accumulator.feed('x', json_line, sizeof(json_line)) ==
               JsonLineResult::NONE);
    }
    assert(accumulator.discarding());
    assert(accumulator.feed('\n', json_line, sizeof(json_line)) ==
           JsonLineResult::OVERSIZE_DROPPED);
    assert(!accumulator.discarding());

    std::puts("esp32_b core ok");
    return 0;
}
