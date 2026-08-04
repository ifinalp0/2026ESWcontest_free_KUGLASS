#include "camera_recovery.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    CameraRecoveryState recovery(2000U, 30000U, 2U);
    assert(recovery.start_due(0U));

    recovery.note_start_result(false, 100U);
    assert(!recovery.running());
    assert(recovery.next_start_ms() == 2100U);
    assert(recovery.retry_delay_ms() == 4000U);
    assert(!recovery.start_due(2099U));
    assert(recovery.start_due(2100U));

    recovery.note_start_result(false, 2100U);
    assert(recovery.next_start_ms() == 6100U);
    assert(recovery.retry_delay_ms() == 8000U);
    recovery.note_start_result(true, 6100U);
    assert(recovery.running());
    assert(recovery.retry_delay_ms() == 2000U);

    assert(!recovery.note_capture_result(false, 6200U));
    assert(recovery.consecutive_capture_failures() == 1U);
    assert(recovery.note_capture_result(false, 6300U));
    assert(!recovery.running());
    assert(recovery.next_start_ms() == 8300U);
    assert(recovery.retry_delay_ms() == 4000U);

    recovery.note_start_result(true, 8300U);
    assert(!recovery.note_capture_result(true, 8400U));
    assert(recovery.running());
    assert(recovery.retry_delay_ms() == 2000U);

    CameraRecoveryState wrapped(2000U, 30000U, 1U);
    wrapped.note_start_result(false, UINT32_MAX - 1000U);
    assert(wrapped.next_start_ms() == 999U);
    assert(!wrapped.start_due(998U));
    assert(wrapped.start_due(999U));

    CameraRecoveryState capped(2000U, 30000U, 2U);
    uint32_t failure_time = 0U;
    for (int i = 0; i < 6; ++i) {
        capped.note_start_result(false, failure_time);
        failure_time = capped.next_start_ms();
    }
    assert(capped.retry_delay_ms() == 30000U);
    capped.note_start_result(false, failure_time);
    assert(capped.retry_delay_ms() == 30000U);
    assert(capped.next_start_ms() == failure_time + 30000U);

    CameraRecoveryState normalized(0U, 0U, 0U);
    normalized.note_start_result(true, 0U);
    assert(normalized.note_capture_result(false, 1U));
    assert(normalized.next_start_ms() == 2U);

    std::puts("camera recovery ok");
    return 0;
}
