#include "downstream_status.h"
#include "esp32_b_link.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kValidStatus =
    "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":50,\"ch\":["
    "{\"id\":0,\"mi\":0.10,\"fault\":false},"
    "{\"id\":1,\"mi\":0.20,\"fault\":false},"
    "{\"id\":2,\"mi\":0.30,\"fault\":true},"
    "{\"id\":3,\"mi\":0.40,\"fault\":false}]}";

bool rejects(const char* line, DownstreamStatusError expected) {
    DownstreamStatus status;
    DownstreamStatusError error;
    return !parse_downstream_status_line(line, &status, &error) && error == expected;
}

}  // namespace

int main() {
    DownstreamStatus status;
    DownstreamStatusError error;
    assert(parse_downstream_status_line(kValidStatus, &status, &error));
    assert(std::fabs(status.channels[3].applied_mi - 0.4f) < 0.0001f);
    assert(status.channels[2].fault);

    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,\"ch\":["
        "{\"id\":0,\"mi\":0.1,\"fault\":false},{\"id\":1,\"mi\":0.2,\"fault\":false}]}",
        DownstreamStatusError::INCOMPLETE_CHANNELS));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,\"ch\":["
        "{\"id\":0,\"mi\":0.1,\"fault\":false},{\"id\":1,\"mi\":0.2,\"fault\":false},"
        "{\"id\":2,\"mi\":0.3,\"fault\":false},{\"id\":2,\"mi\":0.4,\"fault\":false}]}",
        DownstreamStatusError::DUPLICATE_CHANNEL));

    Esp32BLink link;
    assert(link.note_valid_status(1000, 100));
    assert(!link.note_valid_status(1050, 100));
    assert(!link.note_valid_status(1100, 99));
    assert(link.note_valid_status(1150, 101));
    assert(link.note_valid_status(2201, 1));

    Esp32BLink wraparound;
    assert(wraparound.note_valid_status(1000, UINT32_MAX));
    assert(wraparound.note_valid_status(1050, 0));

    std::puts("downstream status parser/sequence ok");
    return 0;
}
