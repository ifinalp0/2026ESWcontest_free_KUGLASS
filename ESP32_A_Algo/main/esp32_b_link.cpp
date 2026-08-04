#include "esp32_b_link.h"

#include "kuglass_config.h"
#include "protocol.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#endif

namespace {
#ifdef ESP_PLATFORM
constexpr uart_port_t kDownstreamUart = UART_NUM_1;
constexpr int kDownstreamBaud = 115200;
#endif
}

bool format_reset_fault_line(const ResetFaultRequest& request,
                             char* output,
                             size_t output_size) {
    if (output == nullptr || output_size == 0U ||
        request.source_session_id == 0U || request.target_boot_id == 0U ||
        request.reset_challenge == 0U) {
        return false;
    }
    const int length = std::snprintf(
        output, output_size,
        "{\"v\":1,\"type\":\"control\",\"seq\":%lu,"
        "\"source_session_id\":%lu,\"target_boot_id\":%lu,"
        "\"reset_challenge\":%lu,\"command\":\"reset_fault\"}",
        static_cast<unsigned long>(request.seq),
        static_cast<unsigned long>(request.source_session_id),
        static_cast<unsigned long>(request.target_boot_id),
        static_cast<unsigned long>(request.reset_challenge));
    return length >= 0 && static_cast<size_t>(length) < output_size;
}

void ResetFaultCoordinator::note_status_context(const DownstreamStatus& status) {
    context_valid_ = status.boot_id != 0U && status.reset_challenge != 0U;
    latest_boot_id_ = status.boot_id;
    latest_reset_challenge_ = status.reset_challenge;
}

bool ResetFaultCoordinator::begin(uint32_t request_seq,
                                  uint32_t source_session_id,
                                  uint32_t now_ms,
                                  ResetFaultRequest* request,
                                  const char** error) {
    if (request == nullptr || error == nullptr) return false;
    if (source_session_id == 0U) {
        *error = "INVALID_SOURCE_SESSION";
        return false;
    }
    if (pending_) {
        *error = "B_RESET_PENDING";
        return false;
    }
    if (!context_valid_) {
        *error = "B_RESET_CONTEXT_UNAVAILABLE";
        return false;
    }
    pending_request_.seq = request_seq;
    pending_request_.source_session_id = source_session_id;
    pending_request_.target_boot_id = latest_boot_id_;
    pending_request_.reset_challenge = latest_reset_challenge_;
    pending_since_ms_ = now_ms;
    pending_ = true;
    *request = pending_request_;
    *error = nullptr;
    return true;
}

void ResetFaultCoordinator::cancel(uint32_t request_seq) {
    if (pending_ && pending_request_.seq == request_seq) pending_ = false;
}

bool ResetFaultCoordinator::note_control_result(const DownstreamStatus& status,
                                                ResetFaultOutcome* outcome) {
    if (outcome == nullptr || !pending_ || !status.has_control_result) return false;
    const DownstreamControlResult& result = status.control_result;
    if (status.boot_id != pending_request_.target_boot_id ||
        result.seq != pending_request_.seq ||
        result.source_session_id != pending_request_.source_session_id) {
        return false;
    }
    outcome->seq = pending_request_.seq;
    outcome->ok = result.ok;
    outcome->error = downstream_control_result_error_name(result.error);
    pending_ = false;
    return true;
}

bool ResetFaultCoordinator::poll_timeout(uint32_t now_ms,
                                         ResetFaultOutcome* outcome) {
    if (outcome == nullptr || !pending_ ||
        static_cast<uint32_t>(now_ms - pending_since_ms_) <
            KUGLASS_B_RESET_TIMEOUT_MS) {
        return false;
    }
    outcome->seq = pending_request_.seq;
    outcome->ok = false;
    outcome->error = "B_RESET_TIMEOUT";
    pending_ = false;
    return true;
}

bool Esp32BLink::begin() {
#ifdef ESP_PLATFORM
    uart_config_t config = {};
    config.baud_rate = kDownstreamBaud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(kDownstreamUart, &config) != ESP_OK ||
        uart_set_pin(kDownstreamUart,
                     KUGLASS_B_UART_TX_GPIO,
                     KUGLASS_B_UART_RX_GPIO,
                     UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(kDownstreamUart, 2048, 2048, 0, nullptr, 0) != ESP_OK) {
        initialized_ = false;
        last_write_ok_ = false;
        error_name_ = "UART_INIT_FAILED";
        return false;
    }
    initialized_ = true;
    last_write_ok_ = true;
    error_name_ = "NONE";
    return true;
#else
    error_name_ = "HOST_BUILD";
    return false;
#endif
}

bool Esp32BLink::send_decision(const PolicyDecision& decision) {
    ProtocolCommand command;
    if (!policy_decision_to_protocol(decision, KUGLASS_DOWNSTREAM_TTL_MS, &command)) {
        last_write_ok_ = false;
        error_name_ = "COMMAND_BUILD_FAILED";
        return false;
    }
    char line[512];
    if (!format_command_line(command, line, sizeof(line))) {
        last_write_ok_ = false;
        error_name_ = "COMMAND_TOO_LARGE";
        return false;
    }
    return write_line(line);
}

bool Esp32BLink::send_reset_fault(const ResetFaultRequest& request) {
    char line[256];
    if (!format_reset_fault_line(request, line, sizeof(line))) {
        last_write_ok_ = false;
        error_name_ = "RESET_BUILD_FAILED";
        return false;
    }
    return write_line(line);
}

bool Esp32BLink::write_line(const char* line) {
#ifdef ESP_PLATFORM
    if (!initialized_ || line == nullptr) {
        last_write_ok_ = false;
        error_name_ = "NOT_INITIALIZED";
        return false;
    }
    const size_t length = std::strlen(line);
    const int payload_written = uart_write_bytes(kDownstreamUart, line, length);
    const int newline_written = payload_written == static_cast<int>(length)
                                    ? uart_write_bytes(kDownstreamUart, "\n", 1)
                                    : -1;
    last_write_ok_ = payload_written == static_cast<int>(length) && newline_written == 1;
    error_name_ = last_write_ok_ ? "NONE" : "UART_WRITE_FAILED";
    return last_write_ok_;
#else
    (void)line;
    last_write_ok_ = false;
    error_name_ = "HOST_BUILD";
    return false;
#endif
}

bool Esp32BLink::read_line(char* output, size_t output_size, uint32_t timeout_ms) {
#ifdef ESP_PLATFORM
    if (!initialized_ || output == nullptr || output_size < 2) {
        return false;
    }
    bool first_read = true;
    while (true) {
        uint8_t byte = 0;
        const TickType_t wait = first_read ? pdMS_TO_TICKS(timeout_ms) : 0;
        first_read = false;
        if (uart_read_bytes(kDownstreamUart, &byte, 1, wait) <= 0) {
            return false;
        }
        const JsonLineResult result = rx_lines_.feed(byte, output, output_size);
        if (result == JsonLineResult::COMPLETE) return true;
        if (result == JsonLineResult::OVERSIZE_DROPPED) {
            error_name_ = "RX_LINE_TOO_LARGE";
            return false;
        }
        if (result == JsonLineResult::OUTPUT_TOO_SMALL) {
            error_name_ = "RX_OUTPUT_TOO_SMALL";
            return false;
        }
    }
#else
    (void)output;
    (void)output_size;
    (void)timeout_ms;
    return false;
#endif
}

bool Esp32BLink::note_valid_status(uint32_t now_ms,
                                  uint32_t boot_id,
                                  uint32_t seq) {
    if (boot_id == 0U) return false;
    const bool received = status_received_.load();
    if (received && boot_id == last_boot_id_.load() &&
        static_cast<int32_t>(seq - last_status_seq_.load()) <= 0) {
        return false;
    }
    last_boot_id_.store(boot_id);
    last_status_seq_.store(seq);
    last_status_ms_.store(now_ms);
    status_received_.store(true);
    return true;
}

bool Esp32BLink::status_healthy(uint32_t now_ms) const {
    return initialized_.load() && status_received_.load() &&
           static_cast<uint32_t>(now_ms - last_status_ms_.load()) <=
               KUGLASS_B_STATUS_TIMEOUT_MS;
}

const char* Esp32BLink::status_name(uint32_t now_ms) const {
    if (!initialized_.load()) return error_name_.load();
    if (!status_received_.load()) {
        return last_write_ok_.load() ? "UART_TX_READY/WAITING_B" : error_name_.load();
    }
    if (static_cast<uint32_t>(now_ms - last_status_ms_.load()) >
        KUGLASS_B_STATUS_TIMEOUT_MS) {
        return "B_STATUS_TIMEOUT";
    }
    return "NONE";
}
