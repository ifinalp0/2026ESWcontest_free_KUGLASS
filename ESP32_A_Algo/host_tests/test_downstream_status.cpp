#include "downstream_status.h"
#include "esp32_b_link.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

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
    assert(std::fabs(status.channels[3].applied_mi - 0.4f) < 0.0001f);
    assert(status.channels[2].fault);

    const DownstreamStatus result_status = parsed(kResultStatus);
    assert(result_status.has_control_result);
    assert(result_status.control_result.seq == 81);
    assert(result_status.control_result.source_session_id == 6001);
    assert(result_status.control_result.ok);
    assert(result_status.control_result.error ==
           DownstreamControlResultError::NONE);

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

    Esp32BLink link;
    assert(link.note_valid_status(1000, 7001, 100));
    assert(!link.note_valid_status(1050, 7001, 100));
    assert(!link.note_valid_status(2201, 7001, 99));
    assert(link.note_valid_status(2202, 7002, 0));

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
