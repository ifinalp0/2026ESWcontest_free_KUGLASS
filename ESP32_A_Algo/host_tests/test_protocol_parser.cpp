#include "protocol.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

int main() {
    ProtocolCommand command;
    ProtocolError error;
    const char* line =
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":1240,\"ttl_ms\":200,\"ch\":["
        "[0,0.72,true],[1,0.55,false],[2,0.20,true],[3,0.30,true]]}";
    assert(parse_command_line(line, &command, &error));
    assert(command.channel_count == 4);
    assert(command.channels[3].channel_id == 3);
    assert(std::fabs(command.channels[0].mi - 0.72f) < 0.001f);

    assert(!parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":1,\"ttl_ms\":200,"
        "\"ch\":[[0,0.5,true]]}", &command, &error));
    assert(error == ProtocolError::BAD_CHANNEL_ARRAY);

    assert(!parse_command_line(
        "{\"v\":1,\"type\":\"actuator_command\",\"seq\":1,\"ttl_ms\":200,\"ch\":["
        "[0,0.5,true],[1,0.5,true],[2,0.5,true],[4,0.5,true]]}", &command, &error));
    assert(error == ProtocolError::BAD_CHANNEL_ARRAY);

    std::string duplicate(line);
    duplicate.replace(duplicate.find("[3,0.30,true]"),
                      std::strlen("[3,0.30,true]"), "[2,0.30,true]");
    assert(!parse_command_line(duplicate.c_str(), &command, &error));

    ProtocolCommand outbound;
    outbound.seq = 88;
    outbound.ttl_ms = 250;
    outbound.channel_count = 4;
    for (size_t i = 0; i < outbound.channel_count; ++i) {
        outbound.channels[i].channel_id = static_cast<uint8_t>(i);
        outbound.channels[i].mi = static_cast<float>(i) / 3.0f;
        outbound.channels[i].enable = i != 2;
    }
    char encoded[384];
    assert(format_command_line(outbound, encoded, sizeof(encoded)));
    ProtocolCommand round_trip;
    assert(parse_command_line(encoded, &round_trip, &error));
    assert(round_trip.channel_count == 4);
    assert(!round_trip.channels[2].enable);
    assert(std::fabs(round_trip.channels[3].mi - 1.0f) < 0.0001f);

    outbound.channel_count = 3;
    assert(!format_command_line(outbound, encoded, sizeof(encoded)));

    std::puts("protocol parser ok");
    return 0;
}
