#pragma once

#include "esp_err.h"

// Software-encodes RGB565 camera frames and streams JPEG forever over UART0.
// Returns only if the UART cannot be initialized or transmission fails.
esp_err_t serial_frame_server_run();
