#include "downstream_status.h"
#include "esp32_b_link.h"
#include "json_line_accumulator.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

constexpr const char* kValidStatus =
    "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\","
    "\"seq\":50,\"boot_id\":7001,\"reset_challenge\":9001,"
    "\"estop\":false,\"fault_code\":\"NONE\",\"diagnostic\":\"BOOT\","
    "\"ch\":["
    "{\"id\":0,\"mi\":0.10,\"fault\":false},"
    "{\"id\":1,\"mi\":0.20,\"fault\":false},"
    "{\"id\":2,\"mi\":0.30,\"fault\":true},"
    "{\"id\":3,\"mi\":0.40,\"fault\":false}],"
    "\"adc\":{\"initialized\":true,\"i_cali\":true,\"t_cali\":false,"
    "\"raw_valid_mask\":255,\"mv_valid_mask\":15,"
    "\"i_raw\":[100,101,102,103],\"t_raw\":[200,201,202,203],"
    "\"i_mv\":[10,11,12,13],\"t_mv\":[1000,1001,1002,1003]},"
    "\"future\":{\"accepted\":true}}";

constexpr const char* kResultStatus =
    "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\","
    "\"seq\":51,\"boot_id\":7001,\"reset_challenge\":9002,"
    "\"estop\":false,\"fault_code\":\"NONE\","
    "\"control_result\":{\"command\":\"reset_fault\",\"seq\":81,"
    "\"source_session_id\":6001,\"ok\":true,\"error\":\"NONE\"},"
    "\"ch\":["
    "{\"id\":0,\"mi\":0,\"fault\":false},"
    "{\"id\":1,\"mi\":0,\"fault\":false},"
    "{\"id\":2,\"mi\":0,\"fault\":false},"
    "{\"id\":3,\"mi\":0,\"fault\":false}]}";

constexpr const char* kResultLastStatus =
    "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\","
    "\"seq\":52,\"boot_id\":7001,\"reset_challenge\":9002,"
    "\"estop\":false,\"fault_code\":\"NONE\",\"ch\":["
    "{\"id\":0,\"mi\":0,\"fault\":false},"
    "{\"id\":1,\"mi\":0,\"fault\":false},"
    "{\"id\":2,\"mi\":0,\"fault\":false},"
    "{\"id\":3,\"mi\":0,\"fault\":false}],"
    "\"control_result\":{\"command\":\"reset_fault\",\"seq\":81,"
    "\"source_session_id\":6001,\"ok\":true,\"error\":\"NONE\"}}";

bool rejects(const char* line, DownstreamStatusError expected) {
    DownstreamStatus status;
    DownstreamStatusError error;
    return !parse_downstream_status_line(line, &status, &error) && error == expected;
}

DownstreamStatus parsed(const char* line) {
    DownstreamStatus status;
    DownstreamStatusError error;
    assert(parse_downstream_status_line(line, &status, &error));
    return status;
}

}  // namespace

int main() {
    assert(is_esp32s3_rom_boot_line("ESP-ROM:esp32s3-20210327"));
    assert(is_esp32s3_rom_boot_line("Build:Mar 27 2021"));
    assert(is_esp32s3_rom_boot_line(
        "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)"));
    assert(is_esp32s3_rom_boot_line("Saved PC:0x403c98d4"));
    assert(is_esp32s3_rom_boot_line("SPIWP:0xee"));
    assert(is_esp32s3_rom_boot_line("mode:DIO, clock div:1"));
    assert(is_esp32s3_rom_boot_line("load:0x3fce3808,len:0x44c"));
    assert(is_esp32s3_rom_boot_line("entry 0x403c98d4"));
    assert(is_esp32s3_rom_boot_line("waiting for download"));
    assert(!is_esp32s3_rom_boot_line(nullptr));
    assert(!is_esp32s3_rom_boot_line(""));
    assert(!is_esp32s3_rom_boot_line("ESP-ROM:esp32-20210327"));
    assert(!is_esp32s3_rom_boot_line(
        "{\"message\":\"ESP-ROM:esp32s3-20210327\"}"));

    std::string recovery_stream =
        "ESP-ROM:esp32s3-20210327\r\n"
        "Build:Mar 27 2021\r\n"
        "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
        "Saved PC:0x403c98d4\r\n"
        "SPIWP:0xee\r\n"
        "mode:DIO, clock div:1\r\n"
        "load:0x3fce3808,len:0x44c\r\n"
        "load:0x403c9700,len:0xbe4\r\n"
        "load:0x403cc700,len:0x2a38\r\n"
        "entry 0x403c98d4\r\n";
    recovery_stream += kValidStatus;
    recovery_stream += '\n';

    JsonLineAccumulator recovery_lines;
    Esp32BLink recovery_link;
    char recovered_line[JsonLineAccumulator::kCapacity] = {};
    size_t rom_lines = 0;
    size_t valid_lines = 0;
    size_t invalid_lines = 0;
    for (const unsigned char byte : recovery_stream) {
        if (recovery_lines.feed(byte, recovered_line, sizeof(recovered_line)) !=
            JsonLineResult::COMPLETE) {
            continue;
        }
        if (is_esp32s3_rom_boot_line(recovered_line)) {
            ++rom_lines;
            recovery_link.note_rom_boot();
            assert(!recovery_link.status_healthy(500));
            assert(recovery_link.status_parse_error() == DownstreamStatusError::OK);
            assert(recovery_link.rom_boot_seen());
            assert(std::strcmp(recovery_link.status_name(500), "B_RESTARTING") == 0);
            continue;
        }
        DownstreamStatus recovered_status;
        DownstreamStatusError recovered_error;
        if (!parse_downstream_status_line(
                recovered_line, &recovered_status, &recovered_error)) {
            recovery_link.note_status_parse_error(recovered_error);
            ++invalid_lines;
            continue;
        }
        if (recovery_link.note_valid_status(
                1000, recovered_status.boot_id, recovered_status.seq)) {
            ++valid_lines;
        }
    }
    assert(rom_lines == 10);
    assert(valid_lines == 1);
    assert(invalid_lines == 0);
    assert(recovery_link.status_parse_error() == DownstreamStatusError::OK);
    assert(!recovery_link.rom_boot_seen());

    ResetFaultCoordinator invalidated_context;
    invalidated_context.note_status_context(parsed(kValidStatus));
    invalidated_context.invalidate_status_context();
    ResetFaultRequest invalidated_request;
    const char* invalidated_error = nullptr;
    assert(!invalidated_context.begin(
        12, 34, 56, &invalidated_request, &invalidated_error));
    assert(std::strcmp(invalidated_error, "B_RESET_CONTEXT_UNAVAILABLE") == 0);

    const DownstreamStatus status = parsed(kValidStatus);
    assert(status.boot_id == 7001);
    assert(status.reset_challenge == 9001);
    assert(!status.estop);
    assert(status.fault_code == DownstreamFaultCode::NONE);
    assert(status.has_diagnostic && std::strcmp(status.diagnostic, "BOOT") == 0);
    assert(status.has_adc && status.adc.initialized);
    assert(status.adc.current_calibrated);
    assert(!status.adc.temperature_calibrated);
    assert(status.adc.raw_valid_mask == 0xFF);
    assert(status.adc.mv_valid_mask == 0x0F);
    assert(status.adc.temperature_mv[3] == 1003);

    // ESP32_B's canonical formatter places adc last. Nested parsers must
    // consume exactly their own closing delimiter and leave the top-level
    // object delimiter for the status parser.
    std::string adc_last_status = kValidStatus;
    const size_t future_field = adc_last_status.rfind(",\"future\"");
    assert(future_field != std::string::npos);
    adc_last_status.erase(future_field);
    adc_last_status += '}';
    const DownstreamStatus adc_last = parsed(adc_last_status.c_str());
    assert(adc_last.has_adc);
    assert(adc_last.adc.raw_valid_mask == 0xFFU);
    assert(std::fabs(status.channels[3].applied_mi - 0.4f) < 0.0001f);
    assert(status.channels[2].fault);

    const DownstreamStatus result_status = parsed(kResultStatus);
    assert(result_status.has_control_result);
    assert(result_status.control_result.seq == 81);
    assert(result_status.control_result.source_session_id == 6001);
    assert(result_status.control_result.ok);
    assert(result_status.control_result.error ==
           DownstreamControlResultError::NONE);

    const DownstreamStatus result_last_status = parsed(kResultLastStatus);
    assert(result_last_status.has_control_result);
    assert(result_last_status.control_result.seq == 81);
    assert(result_last_status.control_result.ok);

    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"reset_challenge\":2,\"estop\":false,\"fault_code\":\"NONE\","
        "\"ch\":[{\"id\":0,\"mi\":0,\"fault\":false},"
        "{\"id\":1,\"mi\":0,\"fault\":false},"
        "{\"id\":2,\"mi\":0,\"fault\":false},"
        "{\"id\":3,\"mi\":0,\"fault\":false}]}",
        DownstreamStatusError::MISSING_BOOT_ID));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":0,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"ch\":[]}",
        DownstreamStatusError::BAD_BOOT_ID));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":1,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"diagnostic\":\"bad-value\",\"ch\":[]}",
        DownstreamStatusError::BAD_DIAGNOSTIC));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":1,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"control_result\":{\"command\":\"reset_fault\","
        "\"seq\":1,\"source_session_id\":3,\"ok\":true,"
        "\"error\":\"RESET_UNSAFE\"},\"ch\":[]}",
        DownstreamStatusError::BAD_CONTROL_RESULT));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":1,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"adc\":{\"initialized\":true},\"ch\":[]}",
        DownstreamStatusError::BAD_ADC));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":1,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"ch\":["
        "{\"id\":0,\"mi\":0.1,\"fault\":false},"
        "{\"id\":1,\"mi\":0.2,\"fault\":false}]}",
        DownstreamStatusError::INCOMPLETE_CHANNELS));
    assert(rejects(
        "{\"v\":1,\"type\":\"status\",\"controller_id\":\"B\",\"seq\":1,"
        "\"boot_id\":1,\"reset_challenge\":2,\"estop\":false,"
        "\"fault_code\":\"NONE\",\"ch\":["
        "{\"id\":0,\"mi\":0.7001,\"fault\":false},"
        "{\"id\":1,\"mi\":0.2,\"fault\":false},"
        "{\"id\":2,\"mi\":0.3,\"fault\":false},"
        "{\"id\":3,\"mi\":0.4,\"fault\":false}]}",
        DownstreamStatusError::BAD_CHANNEL));

    Esp32BLink link;
    assert(link.note_valid_status(1000, 7001, 100));
    assert(!link.note_valid_status(1050, 7001, 100));
    assert(!link.note_valid_status(2201, 7001, 99));
    assert(link.note_valid_status(2202, 7002, 0));

    DownstreamStatus malformed_status;
    DownstreamStatusError malformed_error;
    assert(!parse_downstream_status_line(
        "{\"v\":1,\"type\":\"status\"", &malformed_status, &malformed_error));
    assert(malformed_error == DownstreamStatusError::BAD_JSON);
    link.note_status_parse_error(malformed_error);
    assert(link.status_parse_error() == DownstreamStatusError::BAD_JSON);
    assert(std::strcmp(link.status_name(2203), "BAD_JSON") == 0);
    assert(!link.status_healthy(2203));
    assert(!link.note_valid_status(2204, 7002, 0));
    assert(link.status_parse_error() == DownstreamStatusError::BAD_JSON);
    assert(link.note_valid_status(2205, 7002, 1));
    assert(link.status_parse_error() == DownstreamStatusError::OK);
    assert(std::strcmp(link.status_name(2205), "NOT_INITIALIZED") == 0);

    Esp32BLink wraparound;
    assert(wraparound.note_valid_status(1000, 7001, UINT32_MAX));
    assert(wraparound.note_valid_status(1050, 7001, 0));

    ResetFaultCoordinator reset;
    ResetFaultRequest request;
    const char* start_error = nullptr;
    assert(!reset.begin(81, 6001, 1000, &request, &start_error));
    assert(std::strcmp(start_error, "B_RESET_CONTEXT_UNAVAILABLE") == 0);
    reset.note_status_context(status);
    assert(reset.begin(81, 6001, 1000, &request, &start_error));
    assert(request.seq == 81 && request.source_session_id == 6001);
    assert(request.target_boot_id == 7001 && request.reset_challenge == 9001);
    assert(!reset.begin(82, 6001, 1001, &request, &start_error));
    assert(std::strcmp(start_error, "B_RESET_PENDING") == 0);

    char control_line[256];
    assert(format_reset_fault_line(request, control_line, sizeof(control_line)));
    assert(std::strcmp(
        control_line,
        "{\"v\":1,\"type\":\"control\",\"seq\":81,"
        "\"source_session_id\":6001,\"target_boot_id\":7001,"
        "\"reset_challenge\":9001,\"command\":\"reset_fault\"}") == 0);

    DownstreamStatus mismatch = result_status;
    mismatch.boot_id = 7002;
    ResetFaultOutcome outcome;
    assert(!reset.note_control_result(mismatch, &outcome));
    mismatch = result_status;
    mismatch.control_result.source_session_id = 6002;
    assert(!reset.note_control_result(mismatch, &outcome));
    assert(reset.note_control_result(result_status, &outcome));
    assert(outcome.seq == 81 && outcome.ok &&
           std::strcmp(outcome.error, "NONE") == 0);
    assert(!reset.pending());

    const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 500U;
    reset.note_status_context(status);
    assert(reset.begin(90, 6001, near_wrap, &request, &start_error));
    assert(!reset.poll_timeout(near_wrap + 1499U, &outcome));
    assert(reset.poll_timeout(near_wrap + 1500U, &outcome));
    assert(outcome.seq == 90 && !outcome.ok &&
           std::strcmp(outcome.error, "B_RESET_TIMEOUT") == 0);

    std::puts("downstream status/session/reset correlation ok");
    return 0;
}
