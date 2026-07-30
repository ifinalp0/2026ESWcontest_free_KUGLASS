#include "serial_frame_server.h"

#include "sdkconfig.h"

#include "driver/uart.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

constexpr char kTag[] = "serial_camera";
constexpr uart_port_t kUart = UART_NUM_0;
constexpr int kUartTxPin = 43;
constexpr int kUartRxPin = 44;
constexpr uint8_t kMagic[8] = {'K', 'U', 'G', 'L', 'C', 'A', 'M', '1'};
constexpr uint8_t kFormatJpeg = 2;
constexpr int kRxBufferSize = 256;
constexpr int kTxBufferSize = 8192;

struct __attribute__((packed)) FrameHeader {
    uint8_t magic[sizeof(kMagic)];
    uint32_t sequence;
    uint16_t width;
    uint16_t height;
    uint8_t format;
    uint8_t reserved[3];
    uint32_t payload_size;
    uint32_t payload_fnv1a;
};

static_assert(sizeof(FrameHeader) == 28,
              "The host protocol requires a 28-byte frame header.");

uint32_t fnv1a(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

bool write_all(const void* data, size_t size) {
    const int written = uart_write_bytes(kUart, data, size);
    return written >= 0 && static_cast<size_t>(written) == size;
}

}  // namespace

esp_err_t serial_frame_server_run() {
    ESP_LOGI(kTag,
             "Switching USB-to-UART camera transport to %d baud",
             CONFIG_CAMERA_APP_SERIAL_BAUD);
    ESP_LOGI(kTag,
             "Start host_tools/serial_camera_viewer.py on the computer");
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));

    const uart_config_t uart_config = {
        .baud_rate = CONFIG_CAMERA_APP_SERIAL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    esp_err_t err = uart_param_config(kUart, &uart_config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(kUart, kUartTxPin, kUartRxPin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_driver_install(
        kUart, kRxBufferSize, kTxBufferSize, 0, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = uart_set_baudrate(kUart, CONFIG_CAMERA_APP_SERIAL_BAUD);
    if (err != ESP_OK) {
        return err;
    }

    // From this point UART0 carries a binary stream. Prevent normal log output
    // from interrupting frames; the host parser can still resynchronize after
    // unavoidable ROM output caused by a reset.
    esp_log_level_set("*", ESP_LOG_NONE);

    jpgSetChroma(CHROMA_420);
    jpgSetRgb565BE(true);

    uint32_t sequence = 0;
    for (;;) {
        camera_fb_t* frame = esp_camera_fb_get();
        if (frame == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const size_t expected_raw_size =
            static_cast<size_t>(frame->width) * frame->height * 2;
        if (frame->format != PIXFORMAT_RGB565 ||
            frame->len != expected_raw_size ||
            frame->width > UINT16_MAX ||
            frame->height > UINT16_MAX) {
            esp_camera_fb_return(frame);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const uint16_t width = static_cast<uint16_t>(frame->width);
        const uint16_t height = static_cast<uint16_t>(frame->height);
        uint8_t* jpeg = nullptr;
        size_t jpeg_size = 0;
        const bool encoded = frame2jpg(
            frame, CONFIG_CAMERA_APP_JPEG_QUALITY, &jpeg, &jpeg_size);
        esp_camera_fb_return(frame);

        const bool valid_jpeg =
            encoded && jpeg != nullptr &&
            jpeg_size >= 4 &&
            jpeg[0] == 0xff && jpeg[1] == 0xd8 &&
            jpeg[jpeg_size - 2] == 0xff && jpeg[jpeg_size - 1] == 0xd9;
        if (!valid_jpeg) {
            std::free(jpeg);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        FrameHeader header = {};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.sequence = sequence++;
        header.width = width;
        header.height = height;
        header.format = kFormatJpeg;
        header.payload_size = static_cast<uint32_t>(jpeg_size);
        header.payload_fnv1a = fnv1a(jpeg, jpeg_size);

        const bool sent =
            write_all(&header, sizeof(header)) &&
            write_all(jpeg, jpeg_size) &&
            uart_wait_tx_done(kUart, pdMS_TO_TICKS(2000)) == ESP_OK;
        std::free(jpeg);

        if (!sent) {
            return ESP_FAIL;
        }
    }
}
