#pragma once

#include "esp_err.h"

// ESP32_A-owned OV2640 capture service. No sibling project is a build-time
// dependency.
esp_err_t camera_service_start();
esp_err_t camera_service_stop();
