#include "server_console.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
SemaphoreHandle_t g_console_mutex = nullptr;

constexpr uint8_t kCameraMagic[8] = {'K', 'U', 'G', 'L', 'C', 'A', 'M', '1'};
constexpr uint8_t kCameraFormatJpeg = 2;
constexpr size_t kUsbWriteChunk = 4096;

struct __attribute__((packed)) CameraFrameHeader {
    uint8_t magic[sizeof(kCameraMagic)];
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint8_t reserved[3];
    uint32_t payload_size;
    uint32_t payload_fnv1a;
};

static_assert(sizeof(CameraFrameHeader) == 28,
              "TabUI camera frames require a 28-byte header.");

uint32_t fnv1a(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

bool write_usb_bytes(const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const size_t chunk = size - offset < kUsbWriteChunk
                                 ? size - offset
                                 : kUsbWriteChunk;
        const int written = usb_serial_jtag_write_bytes(
            data + offset, chunk, pdMS_TO_TICKS(500));
        if (written < 0 || static_cast<size_t>(written) != chunk) return false;
        offset += chunk;
    }
    return true;
}
}
#endif

bool server_console_begin() {
#ifdef ESP_PLATFORM
    g_console_mutex = xSemaphoreCreateMutex();
    if (g_console_mutex == nullptr) return false;

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = 32768,
            .rx_buffer_size = 2048,
        };
        if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
            vSemaphoreDelete(g_console_mutex);
            g_console_mutex = nullptr;
            return false;
        }
    }
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_use_driver();
    return true;
#else
    return true;
#endif
}
void server_console_send_line(const char* line) {
    if (line == nullptr) return;
#ifdef ESP_PLATFORM
    if (g_console_mutex == nullptr) return;
    constexpr uint8_t newline = '\n';
    xSemaphoreTake(g_console_mutex, portMAX_DELAY);
    const bool sent =
        write_usb_bytes(reinterpret_cast<const uint8_t*>(line), std::strlen(line)) &&
        write_usb_bytes(&newline, 1U) &&
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(500)) == ESP_OK;
    (void)sent;
    xSemaphoreGive(g_console_mutex);
#else
    std::printf("%s\n", line);
    std::fflush(stdout);
#endif
}

void server_console_send_ack(const UiCommand& command, bool ok, const char* error) {
    char line[256];
    const char* command_name = ui_command_type_name(command.type);
    if (command.has_seq) {
        if (ok) {
            std::snprintf(line, sizeof(line),
                          "{\"type\":\"ack\",\"seq\":%lu,\"command\":\"%s\",\"ok\":true}",
                          static_cast<unsigned long>(command.seq), command_name);
        } else {
            std::snprintf(line, sizeof(line),
                          "{\"type\":\"ack\",\"seq\":%lu,\"command\":\"%s\",\"ok\":false,\"error\":\"%s\"}",
                          static_cast<unsigned long>(command.seq), command_name,
                          error == nullptr ? "REJECTED" : error);
        }
    } else if (ok) {
        std::snprintf(line, sizeof(line),
                      "{\"type\":\"ack\",\"seq\":null,\"command\":\"%s\",\"ok\":true}",
                      command_name);
    } else {
        std::snprintf(line, sizeof(line),
                      "{\"type\":\"ack\",\"seq\":null,\"command\":\"%s\",\"ok\":false,\"error\":\"%s\"}",
                      command_name, error == nullptr ? "REJECTED" : error);
    }
    server_console_send_line(line);
}

void server_console_send_protocol_error(const char* source, const char* error) {
    char line[192];
    std::snprintf(line, sizeof(line),
                  "{\"type\":\"protocol_error\",\"source\":\"%s\",\"error\":\"%s\"}",
                  source == nullptr ? "unknown" : source,
                  error == nullptr ? "UNKNOWN" : error);
    server_console_send_line(line);
}

bool server_console_send_camera_frame(uint32_t sequence,
                                      uint16_t width,
                                      uint16_t height,
                                      const uint8_t* jpeg,
                                      size_t jpeg_size) {
#ifdef ESP_PLATFORM
    if (g_console_mutex == nullptr || jpeg == nullptr || jpeg_size < 4U ||
        jpeg_size > UINT32_MAX || width == 0U || height == 0U) {
        return false;
    }

    CameraFrameHeader header = {};
    std::memcpy(header.magic, kCameraMagic, sizeof(kCameraMagic));
    header.sequence = sequence;
    header.width = width;
    header.height = height;
    header.format = kCameraFormatJpeg;
    header.payload_size = static_cast<uint32_t>(jpeg_size);
    header.payload_fnv1a = fnv1a(jpeg, jpeg_size);

    xSemaphoreTake(g_console_mutex, portMAX_DELAY);
    std::fflush(stdout);
    const bool sent =
        write_usb_bytes(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) &&
        write_usb_bytes(jpeg, jpeg_size) &&
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000)) == ESP_OK;
    xSemaphoreGive(g_console_mutex);
    return sent;
#else
    (void)sequence;
    (void)width;
    (void)height;
    (void)jpeg;
    (void)jpeg_size;
    return false;
#endif
}
