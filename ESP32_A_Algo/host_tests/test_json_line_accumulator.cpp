#include "json_line_accumulator.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

int main() {
    JsonLineAccumulator accumulator;
    char output[JsonLineAccumulator::kCapacity] = {};

    std::string oversized(JsonLineAccumulator::kCapacity + 32, 'x');
    oversized += "{\"v\":1}\n";
    JsonLineResult result = JsonLineResult::NONE;
    for (const unsigned char byte : oversized) {
        result = accumulator.feed(byte, output, sizeof(output));
    }
    assert(result == JsonLineResult::OVERSIZE_DROPPED);
    assert(!accumulator.discarding());
    assert(output[0] == '\0');

    const char* valid = "{\"v\":1}\r\n";
    for (size_t i = 0; valid[i] != '\0'; ++i) {
        result = accumulator.feed(static_cast<uint8_t>(valid[i]), output, sizeof(output));
    }
    assert(result == JsonLineResult::COMPLETE);
    assert(std::strcmp(output, "{\"v\":1}") == 0);

    JsonLineAccumulator small_output_accumulator;
    char too_small[4] = {};
    for (const char byte : std::string("1234\n")) {
        result = small_output_accumulator.feed(static_cast<uint8_t>(byte),
                                               too_small, sizeof(too_small));
    }
    assert(result == JsonLineResult::OUTPUT_TOO_SMALL);

    std::puts("json line accumulator ok");
    return 0;
}
