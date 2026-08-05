#pragma once

#include "ui_protocol.h"

#include <cstddef>
#include <cstdint>

bool server_console_begin();
void server_console_send_line(const char* line);
void server_console_send_ack(const UiCommand& command, bool ok, const char* error);
void server_console_send_protocol_error(const char* source, const char* error);
bool server_console_send_camera_frame(uint32_t sequence,
                                      uint16_t width,
                                      uint16_t height,
                                      const uint8_t* jpeg,
                                      size_t jpeg_size);
