#pragma once

#include "json_line_accumulator.h"
#include "policy_engine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class Esp32BLink {
public:
    bool begin();
    bool send_decision(const PolicyDecision& decision);
    bool send_reset_fault(uint32_t request_seq);
    bool read_line(char* output, size_t output_size, uint32_t timeout_ms);
    // Accepts only wrap-safe forward progress while the link is active.
    // After a status timeout, the first full validated status rebases the seq.
    bool note_valid_status(uint32_t now_ms, uint32_t seq);
    bool initialized() const { return initialized_.load(); }
    bool status_healthy(uint32_t now_ms) const;
    const char* status_name(uint32_t now_ms) const;
    const char* error_name() const { return error_name_.load(); }

private:
    bool write_line(const char* line);

    std::atomic<bool> initialized_{false};
    std::atomic<bool> last_write_ok_{false};
    std::atomic<const char*> error_name_{"NOT_INITIALIZED"};
    std::atomic<bool> status_received_{false};
    std::atomic<uint32_t> last_status_ms_{0};
    std::atomic<uint32_t> last_status_seq_{0};
    [[maybe_unused]] JsonLineAccumulator rx_lines_;
};
