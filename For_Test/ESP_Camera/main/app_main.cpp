#include "camera_service.h"
#include "serial_frame_server.h"

#include "esp_err.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "ov2640_app";

}  // namespace

extern "C" void app_main(void) {
    esp_err_t err = camera_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Camera startup stopped: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(kTag, "OV2640 wired camera is ready");
    err = serial_frame_server_run();
    ESP_LOGE(kTag, "Serial frame server stopped: %s", esp_err_to_name(err));
}
