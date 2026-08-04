#pragma once

#include "downstream_status.h"
#include "json_line_accumulator.h"
#include "policy_engine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

struct ResetFaultRequest {
    uint32_t seq = 0;
    uint32_t source_session_id = 0;
    uint32_t target_boot_id = 0;
    uint32_t reset_challenge = 0;
};

struct ResetFaultOutcome {
    uint32_t seq = 0;
    bool ok = false;
    const char* error = "UNKNOWN";
};

bool format_reset_fault_line(const ResetFaultRequest& request,
                             char* output,
                             size_t output_size);

// Access is serialized by ESP32_A's state mutex. Keeping the coordinator
// separate from UART I/O makes challenge/correlation/timeout behavior host
// testable and prevents a successful write from being treated as reset success.
class ResetFaultCoordinator {
public:
    void note_status_context(const DownstreamStatus& status);
    bool begin(uint32_t request_seq,
               uint32_t source_session_id,
               uint32_t now_ms,
               ResetFaultRequest* request,
               const char** error);
    void cancel(uint32_t request_seq);
    bool note_control_result(const DownstreamStatus& status,
                             ResetFaultOutcome* outcome);
    bool poll_timeout(uint32_t now_ms, ResetFaultOutcome* outcome);
    bool pending() const { return pending_; }

private:
    bool context_valid_ = false;
    uint32_t latest_boot_id_ = 0;
    uint32_t latest_reset_challenge_ = 0;
    bool pending_ = false;
    ResetFaultRequest pending_request_;
    uint32_t pending_since_ms_ = 0;
};

class Esp32BLink {
public:
    bool begin();
    bool send_decision(const PolicyDecision& decision);
    bool send_reset_fault(const ResetFaultRequest& request);
    bool read_line(char* output, size_t output_size, uint32_t timeout_ms);
    // A new B boot_id rebases seq immediately. Within one boot, only wrap-safe
    // forward progress is accepted, even after a communications timeout.
    bool note_valid_status(uint32_t now_ms, uint32_t boot_id, uint32_t seq);
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
    std::atomic<uint32_t> last_boot_id_{0};
    [[maybe_unused]] JsonLineAccumulator rx_lines_;
};
