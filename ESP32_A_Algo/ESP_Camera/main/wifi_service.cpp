#include "wifi_service.h"

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace {

constexpr char kTag[] = "wifi";

#if CONFIG_CAMERA_APP_WIFI_STATION
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kFailedBit = BIT1;

EventGroupHandle_t g_station_event_group = nullptr;
int g_station_retry_count = 0;
esp_event_handler_instance_t g_wifi_event_instance;
esp_event_handler_instance_t g_ip_event_instance;

void station_event_handler(void*, esp_event_base_t event_base, int32_t event_id,
                           void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Could not start station connection: %s",
                     esp_err_to_name(err));
            xEventGroupSetBits(g_station_event_group, kFailedBit);
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_station_retry_count < CONFIG_CAMERA_APP_STA_MAX_RETRY) {
            ++g_station_retry_count;
            ESP_LOGW(kTag, "Wi-Fi disconnected; retry %d/%d",
                     g_station_retry_count, CONFIG_CAMERA_APP_STA_MAX_RETRY);
            const esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "Could not retry station connection: %s",
                         esp_err_to_name(err));
                xEventGroupSetBits(g_station_event_group, kFailedBit);
            }
        } else {
            xEventGroupSetBits(g_station_event_group, kFailedBit);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
        ESP_LOGI(kTag, "Station address: " IPSTR, IP2STR(&event->ip_info.ip));
        g_station_retry_count = 0;
        xEventGroupClearBits(g_station_event_group, kFailedBit);
        xEventGroupSetBits(g_station_event_group, kConnectedBit);
    }
}
#endif

#if CONFIG_CAMERA_APP_WIFI_SOFTAP
esp_err_t start_softap() {
    const char* ssid = CONFIG_CAMERA_APP_AP_SSID;
    const char* password = CONFIG_CAMERA_APP_AP_PASSWORD;
    const std::size_t ssid_length = std::strlen(ssid);
    const std::size_t password_length = std::strlen(password);

    if (ssid_length == 0 || ssid_length > sizeof(wifi_config_t{}.ap.ssid)) {
        ESP_LOGE(kTag, "SoftAP SSID must contain 1-32 bytes");
        return ESP_ERR_INVALID_ARG;
    }
    if (password_length != 0 && (password_length < 8 || password_length > 63)) {
        ESP_LOGE(kTag, "SoftAP password must be empty or contain 8-63 bytes");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t* netif = esp_netif_create_default_wifi_ap();
    if (netif == nullptr) {
        ESP_LOGE(kTag, "Failed to create the SoftAP network interface");
        return ESP_FAIL;
    }

    wifi_config_t wifi_config = {};
    std::memcpy(wifi_config.ap.ssid, ssid, ssid_length);
    std::memcpy(wifi_config.ap.password, password, password_length);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(ssid_length);
    wifi_config.ap.channel = CONFIG_CAMERA_APP_AP_CHANNEL;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode =
        password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    esp_netif_ip_info_t ip_info = {};
    err = esp_netif_get_ip_info(netif, &ip_info);
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "SoftAP '%s' ready", ssid);
        ESP_LOGI(kTag, "Open http://" IPSTR "/ after joining the AP",
                 IP2STR(&ip_info.ip));
    }
    return err;
}
#endif

#if CONFIG_CAMERA_APP_WIFI_STATION
esp_err_t start_station() {
    const char* ssid = CONFIG_CAMERA_APP_STA_SSID;
    const char* password = CONFIG_CAMERA_APP_STA_PASSWORD;
    const std::size_t ssid_length = std::strlen(ssid);
    const std::size_t password_length = std::strlen(password);

    if (ssid_length == 0 || ssid_length > sizeof(wifi_config_t{}.sta.ssid) ||
        (password_length != 0 &&
         (password_length < 8 ||
          password_length > sizeof(wifi_config_t{}.sta.password) - 1))) {
        ESP_LOGE(kTag, "Configure a valid station SSID/password in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }

    g_station_event_group = xEventGroupCreate();
    if (g_station_event_group == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    if (esp_netif_create_default_wifi_sta() == nullptr) {
        ESP_LOGE(kTag, "Failed to create the station network interface");
        return ESP_FAIL;
    }

    esp_err_t err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &station_event_handler, nullptr,
        &g_wifi_event_instance);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &station_event_handler, nullptr,
        &g_ip_event_instance);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {};
    std::memcpy(wifi_config.sta.ssid, ssid, ssid_length);
    std::memcpy(wifi_config.sta.password, password, password_length);
    wifi_config.sta.threshold.authmode =
        password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    // Station modem sleep can add visible latency to an MJPEG stream.
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(kTag, "Connecting to '%s'", ssid);
    const TickType_t timeout = pdMS_TO_TICKS(
        static_cast<uint32_t>(
            CONFIG_CAMERA_APP_STA_CONNECT_TIMEOUT_SECONDS) *
        1000U);
    const EventBits_t bits = xEventGroupWaitBits(
        g_station_event_group, kConnectedBit | kFailedBit, pdFALSE, pdFALSE,
        timeout);

    if ((bits & kConnectedBit) != 0) {
        return ESP_OK;
    }

    if ((bits & kFailedBit) != 0) {
        ESP_LOGE(kTag, "Could not connect to '%s'", ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(kTag, "Connection to '%s' timed out after %d seconds", ssid,
             CONFIG_CAMERA_APP_STA_CONNECT_TIMEOUT_SECONDS);
    return ESP_ERR_TIMEOUT;
}
#endif

}  // namespace

esp_err_t wifi_service_start() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_CAMERA_APP_WIFI_SOFTAP
    return start_softap();
#else
    return start_station();
#endif
}
