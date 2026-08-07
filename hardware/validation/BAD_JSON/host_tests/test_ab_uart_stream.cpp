#include "downstream_status.h"
#include "json_line_accumulator.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" bool build_b_status_fixture(char* output, std::size_t output_size);

int main() {
    char status_line[JsonLineAccumulator::kCapacity] = {};
    assert(build_b_status_fixture(status_line, sizeof(status_line)));
    assert(std::strlen(status_line) < JsonLineAccumulator::kCapacity - 1U);

    std::string stream =
        "ESP-ROM:esp32s3-20210327\r\n"
        "Build:Mar 27 2021\r\n"
        "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
        "SPIWP:0xee\r\n"
        "mode:DIO, clock div:1\r\n"
        "load:0x3fce3808,len:0x44c\r\n"
        "entry 0x403c98d4\r\n";
    stream += status_line;
    stream += '\n';

    JsonLineAccumulator accumulator;
    char recovered[JsonLineAccumulator::kCapacity] = {};
    size_t rom_line_count = 0;
    size_t status_count = 0;
    for (const unsigned char byte : stream) {
        if (accumulator.feed(byte, recovered, sizeof(recovered)) !=
            JsonLineResult::COMPLETE) {
            continue;
        }
        if (is_esp32s3_rom_boot_line(recovered)) {
            ++rom_line_count;
            continue;
        }

        DownstreamStatus parsed;
        DownstreamStatusError error;
        const bool parsed_ok =
            parse_downstream_status_line(recovered, &parsed, &error);
        if (!parsed_ok) {
            std::fprintf(stderr, "A rejected B status: %s\n%s\n",
                         downstream_status_error_name(error), recovered);
        }
        assert(parsed_ok);
        assert(error == DownstreamStatusError::OK);
        assert(parsed.boot_id == UINT32_MAX);
        assert(parsed.reset_challenge == UINT32_MAX - 1U);
        assert(parsed.seq == 1234U);
        assert(parsed.has_diagnostic);
        assert(std::strcmp(parsed.diagnostic, "BOOT") == 0);
        assert(parsed.has_adc);
        assert(parsed.adc.raw_valid_mask == 0xFFU);
        ++status_count;
    }

    assert(rom_line_count == 7U);
    assert(status_count == 1U);
    std::puts("A-B UART ROM-noise/status compatibility ok");
    return 0;
}
