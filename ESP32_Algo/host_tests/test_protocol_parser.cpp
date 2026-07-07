#include "protocol.h"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    ProtocolCommand command;
    ProtocolError error;
    const char* line = "{\"seq\":1240,\"ttl_ms\":200,\"ch\":[[0,0.72],[1,0.55,0],[4,1.5,true]]}";
    assert(parse_command_line(line, &command, &error));
    assert(error == ProtocolError::OK);
    assert(command.seq == 1240);
    assert(command.ttl_ms == 200);
    assert(command.channel_count == 3);
    assert(command.channels[0].channel_id == 0);
    assert(std::fabs(command.channels[0].mi - 0.72f) < 0.001f);
    assert(command.channels[1].enable == false);
    assert(command.channels[2].channel_id == 4);
    assert(std::fabs(command.channels[2].mi - 1.0f) < 0.001f);

    const char* bad = "{\"ttl_ms\":200,\"ch\":[[0,0.72]]}";
    assert(!parse_command_line(bad, &command, &error));
    assert(error == ProtocolError::MISSING_SEQ);

    std::puts("protocol parser ok");
    return 0;
}

