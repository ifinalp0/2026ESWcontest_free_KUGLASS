#include "channel_manager.h"
#include "fault_manager.h"
#include "protocol.h"
#include "status_reporter.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
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
