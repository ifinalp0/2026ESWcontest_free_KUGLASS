#include "kuglass_config.h"

#include <cassert>
#include <cstdio>

int main() {
    static_assert(KUGLASS_CONTROL_TASK_PRIORITY == 8);
    static_assert(KUGLASS_CONTROL_TASK_PRIORITY > KUGLASS_UI_TASK_PRIORITY);
    static_assert(KUGLASS_UI_TASK_PRIORITY > KUGLASS_SENSOR_TASK_PRIORITY);
    static_assert(KUGLASS_SENSOR_TASK_PRIORITY > KUGLASS_LINK_TASK_PRIORITY);
    static_assert(KUGLASS_LINK_TASK_PRIORITY > KUGLASS_CAMERA_TASK_PRIORITY);
    static_assert(KUGLASS_CAMERA_TASK_PRIORITY > KUGLASS_CAMERA_TX_TASK_PRIORITY);
    assert(KUGLASS_CONTROL_PERIOD_MS == 50U);
    assert(KUGLASS_CAMERA_STREAM_INTERVAL_MS >= KUGLASS_CONTROL_PERIOD_MS * 4U);
    std::puts("task priority contract ok");
    return 0;
}
