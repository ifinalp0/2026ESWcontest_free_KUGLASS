#include "camera_service.h"

#include "camera_pins.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "camera";
constexpr unsigned kInitialCaptureAttempts = 5;
constexpr TickType_t kInitialCaptureSettleDelay = pdMS_TO_TICKS(500);
constexpr TickType_t kInitialCaptureRetryDelay = pdMS_TO_TICKS(250);
bool g_started = false;

camera_config_t make_camera_config() {
    camera_config_t config = {};
    config.pin_pwdn = camera_pins::kPwdn;
    config.pin_reset = camera_pins::kReset;
    config.pin_xclk = camera_pins::kXclk;
    config.pin_sccb_sda = camera_pins::kSiod;
    config.pin_sccb_scl = camera_pins::kSioc;
    config.pin_d7 = camera_pins::kD7;
    config.pin_d6 = camera_pins::kD6;
    config.pin_d5 = camera_pins::kD5;
    config.pin_d4 = camera_pins::kD4;
    config.pin_d3 = camera_pins::kD3;
    config.pin_d2 = camera_pins::kD2;
    config.pin_d1 = camera_pins::kD1;
    config.pin_d0 = camera_pins::kD0;
    config.pin_vsync = camera_pins::kVsync;
    config.pin_href = camera_pins::kHref;
    config.pin_pclk = camera_pins::kPclk;
    config.xclk_freq_hz = camera_pins::kXclkFrequencyHz;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    return config;
}

}  // namespace

esp_err_t camera_service_start() {
    if (g_started) return ESP_OK;

    const camera_config_t config = make_camera_config();
    ESP_LOGI(kTag, "GPIO profile: %s", camera_pins::kProfileName);
    ESP_LOGI(kTag, "On-board XCLK: %d Hz; DCLK/PCLK GPIO: %d",
             camera_pins::kXclkFrequencyHz, camera_pins::kPclk);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_camera_init failed: %s (0x%x)",
                 esp_err_to_name(err), static_cast<unsigned>(err));
        return err;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr || sensor->id.PID != OV2640_PID) {
        if (sensor == nullptr) {
            ESP_LOGE(kTag, "Camera sensor information is unavailable");
            err = ESP_FAIL;
        } else {
            ESP_LOGE(kTag, "Expected OV2640 PID 0x%04x, detected 0x%04x",
                     static_cast<unsigned>(OV2640_PID),
                     static_cast<unsigned>(sensor->id.PID));
            err = ESP_ERR_NOT_SUPPORTED;
        }
        esp_camera_deinit();
        return err;
    }

    vTaskDelay(kInitialCaptureSettleDelay);
    camera_fb_t* frame = nullptr;
    for (unsigned attempt = 1; attempt <= kInitialCaptureAttempts; ++attempt) {
        frame = esp_camera_fb_get();
        if (frame != nullptr) break;
        ESP_LOGW(kTag, "Initial frame attempt %u/%u failed",
                 attempt, kInitialCaptureAttempts);
        if (attempt < kInitialCaptureAttempts) vTaskDelay(kInitialCaptureRetryDelay);
    }
    if (frame == nullptr) {
        ESP_LOGE(kTag, "Initial frame capture failed after %u attempts",
                 kInitialCaptureAttempts);
        esp_camera_deinit();
        return ESP_FAIL;
    }

    const bool expected_frame = frame->format == PIXFORMAT_RGB565 &&
                                frame->width == 320U && frame->height == 240U &&
                                frame->len == 320U * 240U * 2U;
    ESP_LOGI(kTag, "Initial RGB565 frame: %ux%u, %u bytes",
             static_cast<unsigned>(frame->width),
             static_cast<unsigned>(frame->height),
             static_cast<unsigned>(frame->len));
    esp_camera_fb_return(frame);
    if (!expected_frame) {
        ESP_LOGE(kTag, "Initial frame does not match the QVGA RGB565 contract");
        esp_camera_deinit();
        return ESP_ERR_INVALID_SIZE;
    }

    g_started = true;
    return ESP_OK;
}

esp_err_t camera_service_stop() {
    if (!g_started) return ESP_OK;
    const esp_err_t err = esp_camera_deinit();
    g_started = false;
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_camera_deinit failed: %s (0x%x)",
                 esp_err_to_name(err), static_cast<unsigned>(err));
    }
    return err;
}
