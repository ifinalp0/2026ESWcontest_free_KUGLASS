#include "channel_manager.h"
#include "fault_manager.h"
#include "kuglass_b_config.h"
#include "power_stage_pinmap.h"
#include "protocol.h"
#include "status_reporter.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    const PowerStagePinmap& pinmap = KUGLASS_POWER_STAGE_PINMAP;
    assert(pinmap.estop_n_gpio == 19);

    const int expected[4][6] = {
        {10, 11, 12, 13, 1, 2},
        {14, 15, 16, 17, 4, 5},
        {18, 21, 38, 39, 6, 7},
        {40, 41, 42, 47, 8, 3},
    };
    for (size_t i = 0; i < KUGLASS_CHANNEL_COUNT; ++i) {
        const PowerStageChannelPins& channel = pinmap.channels[i];
        assert(channel.pwm_gpio == expected[i][0]);
        assert(channel.direction_gpio == expected[i][1]);
        assert(channel.enable_gpio == expected[i][2]);
        assert(channel.fault_n_gpio == expected[i][3]);
        assert(channel.current_adc_gpio == expected[i][4]);
        assert(channel.temperature_adc_gpio == expected[i][5]);
    }
    assert(kuglass_power_stage_pinmap_is_unique());
    assert(KUGLASS_A_UART_TX_GPIO == 43);
    assert(KUGLASS_A_UART_RX_GPIO == 44);
    assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_TX_GPIO));
    assert(!kuglass_power_stage_owns_gpio(KUGLASS_A_UART_RX_GPIO));

    ProtocolCommand command;
    ProtocolError error;
    assert(parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":7,\"ttl_ms\":250,\"ch\":["
        "[0,0.2,true],[1,0.4,true],[2,0.6,true],[3,0.8,true]]}",
        &command, &error));

    ChannelManager channels;
    channels.begin();
    channels.apply_command(command);
    channels.update(1.0f, true);
    assert(channels.count() == 4);
    assert(channels.channel(3)->applied_mi == 0.7f);

    FaultManager fault;
    fault.begin();
    fault.note_command(100, 250);
    fault.update(200, true, true);
    assert(!fault.faulted());
    fault.update(351, true, true);
    assert(fault.code() == FaultCode::COMM_TIMEOUT);
    fault.note_command(400, 250);
    assert(!fault.faulted());
    fault.update(450, false, true);
    assert(fault.code() == FaultCode::ESTOP);
    assert(!fault.clear_if_safe(false, true));
    assert(fault.clear_if_safe(true, true));

    char status[512];
    assert(format_status_line(7, channels, fault, false, status, sizeof(status)));
    assert(std::strstr(status, "\"controller_id\":\"B\"") != nullptr);
    assert(std::strstr(status, "\"id\":3") != nullptr);
    assert(std::strstr(status, "\"id\":4") == nullptr);

    std::puts("esp32_b core ok");
    return 0;
}
