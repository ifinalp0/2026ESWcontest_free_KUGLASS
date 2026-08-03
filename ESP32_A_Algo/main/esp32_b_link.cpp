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

bool Esp32BLink::send_reset_fault(uint32_t request_seq) {
    char line[96];
    const int length = std::snprintf(
        line, sizeof(line),
        "{\"type\":\"control\",\"seq\":%lu,\"command\":\"reset_fault\"}",
        static_cast<unsigned long>(request_seq));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(line)) {
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

bool Esp32BLink::note_valid_status(uint32_t now_ms, uint32_t seq) {
    const bool received = status_received_.load();
    const bool active = received &&
        static_cast<uint32_t>(now_ms - last_status_ms_.load()) <=
            KUGLASS_B_STATUS_TIMEOUT_MS;
    if (active && static_cast<int32_t>(seq - last_status_seq_.load()) <= 0) {
        return false;
    }
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
