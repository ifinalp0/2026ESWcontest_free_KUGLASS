#pragma once

#include "ui_protocol.h"

void server_console_begin();
void server_console_send_line(const char* line);
void server_console_send_ack(const UiCommand& command, bool ok, const char* error);
void server_console_send_protocol_error(const char* source, const char* error);
