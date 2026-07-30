#include "http_stream_server.h"

#include <cstdio>
#include <cstring>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace {

constexpr char kTag[] = "http";
constexpr char kStreamContentType[] =
    "multipart/x-mixed-replace;boundary=frame-7e2f93c1";
constexpr char kStreamBoundary[] = "\r\n--frame-7e2f93c1\r\n";
constexpr char kStreamPart[] =
    "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

constexpr char kIndexHtml[] = R"HTML(
<!doctype html>
<html lang="ko">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 OV2640 Camera</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 0; background: #101418; color: #edf2f7; }
    main { width: min(960px, 94vw); margin: 24px auto; }
    h1 { margin-bottom: 6px; font-size: 1.45rem; }
    p { color: #aebbc8; margin-top: 0; }
    img { display: block; width: 100%; min-height: 180px; object-fit: contain;
          background: #050607; border-radius: 10px; }
    nav { display: flex; gap: 10px; margin-top: 14px; }
    a { color: #101418; background: #63d6b6; padding: 9px 13px;
        border-radius: 7px; text-decoration: none; font-weight: 650; }
  </style>
</head>
<body>
  <main>
    <h1>ESP32 · OV2640</h1>
    <p>Wi-Fi MJPEG live stream</p>
    <img id="camera" alt="OV2640 live stream">
    <nav>
      <a href="/capture" target="_blank">사진 저장</a>
      <a href="/health" target="_blank">상태 확인</a>
    </nav>
  </main>
  <script>
    document.getElementById("camera").src =
      "http://" + window.location.hostname + ":81/stream";
  </script>
</body>
</html>
)HTML";

httpd_handle_t g_control_server = nullptr;
httpd_handle_t g_stream_server = nullptr;

esp_err_t set_common_headers(httpd_req_t* request) {
    return httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache");
}

esp_err_t index_handler(httpd_req_t* request) {
    esp_err_t err =
        httpd_resp_set_type(request, "text/html; charset=utf-8");
    if (err == ESP_OK) {
        err = set_common_headers(request);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send(request, kIndexHtml, HTTPD_RESP_USE_STRLEN);
    }
    return err;
}

esp_err_t health_handler(httpd_req_t* request) {
    char response[192];
    const unsigned free_heap = static_cast<unsigned>(esp_get_free_heap_size());
    const unsigned psram = static_cast<unsigned>(
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    const int length = std::snprintf(
        response, sizeof(response),
        "{\"status\":\"ok\",\"camera\":\"ov2640\",\"free_heap\":%u,"
        "\"psram_bytes\":%u}\n",
        free_heap, psram);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(response)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON buffer error");
    }

    esp_err_t err =
        httpd_resp_set_type(request, "application/json; charset=utf-8");
    if (err == ESP_OK) {
        err = set_common_headers(request);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send(request, response,
                              static_cast<std::size_t>(length));
    }
    return err;
}

esp_err_t capture_handler(httpd_req_t* request) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
        ESP_LOGE(kTag, "Still capture failed");
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
    }

    esp_err_t err = ESP_OK;
    if (frame->format != PIXFORMAT_JPEG) {
        err = httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera is not in JPEG mode");
    } else {
        err = httpd_resp_set_type(request, "image/jpeg");
        if (err == ESP_OK) {
            err = httpd_resp_set_hdr(
                request, "Content-Disposition",
                "inline; filename=ov2640-capture.jpg");
        }
        if (err == ESP_OK) {
            err = set_common_headers(request);
        }
        if (err == ESP_OK) {
            err = httpd_resp_send(
                request, reinterpret_cast<const char*>(frame->buf), frame->len);
        }
    }

    esp_camera_fb_return(frame);
    return err;
}

esp_err_t stream_handler(httpd_req_t* request) {
    esp_err_t result = httpd_resp_set_type(request, kStreamContentType);
    if (result == ESP_OK) {
        result = set_common_headers(request);
    }
    if (result != ESP_OK) {
        return result;
    }

    unsigned frame_count = 0;
    int64_t report_started_us = esp_timer_get_time();
    ESP_LOGI(kTag, "MJPEG client connected");

    while (result == ESP_OK) {
        camera_fb_t* frame = esp_camera_fb_get();
        if (frame == nullptr) {
            ESP_LOGE(kTag, "Stream capture failed");
            result = ESP_FAIL;
            break;
        }

        if (frame->format != PIXFORMAT_JPEG) {
            esp_camera_fb_return(frame);
            ESP_LOGE(kTag, "Unexpected non-JPEG frame");
            result = ESP_FAIL;
            break;
        }

        char part_header[96];
        const int header_length = std::snprintf(
            part_header, sizeof(part_header), kStreamPart, frame->len);
        if (header_length < 0 ||
            static_cast<std::size_t>(header_length) >= sizeof(part_header)) {
            result = ESP_ERR_INVALID_SIZE;
        }
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(
                request, kStreamBoundary, std::strlen(kStreamBoundary));
        }
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(
                request, part_header, static_cast<std::size_t>(header_length));
        }
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(
                request, reinterpret_cast<const char*>(frame->buf), frame->len);
        }

        esp_camera_fb_return(frame);

        ++frame_count;
        if ((frame_count % 100U) == 0U) {
            const int64_t now_us = esp_timer_get_time();
            const double seconds =
                static_cast<double>(now_us - report_started_us) / 1000000.0;
            ESP_LOGI(kTag, "Stream rate: %.1f fps", frame_count / seconds);
            frame_count = 0;
            report_started_us = now_us;
        }
    }

    ESP_LOGI(kTag, "MJPEG client disconnected (%s)",
             result == ESP_OK ? "closed" : esp_err_to_name(result));
    return result;
}

esp_err_t register_handler(httpd_handle_t server, const char* uri,
                           esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = HTTP_GET;
    route.handler = handler;
    route.user_ctx = nullptr;
    return httpd_register_uri_handler(server, &route);
}

}  // namespace

esp_err_t http_stream_server_start() {
    httpd_config_t control_config = HTTPD_DEFAULT_CONFIG();
    control_config.server_port = 80;
    control_config.max_uri_handlers = 4;
    control_config.max_open_sockets = 2;
    control_config.lru_purge_enable = true;
    control_config.stack_size = 6144;

    esp_err_t err = httpd_start(&g_control_server, &control_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Could not start control server: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = register_handler(g_control_server, "/", &index_handler);
    if (err == ESP_OK) {
        err = register_handler(g_control_server, "/capture", &capture_handler);
    }
    if (err == ESP_OK) {
        err = register_handler(g_control_server, "/health", &health_handler);
    }
    if (err != ESP_OK) {
        httpd_stop(g_control_server);
        g_control_server = nullptr;
        return err;
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = control_config.ctrl_port + 1;
    stream_config.max_uri_handlers = 1;
    stream_config.max_open_sockets = 1;
    stream_config.lru_purge_enable = true;
    stream_config.stack_size = 6144;
    stream_config.send_wait_timeout = 5;

    err = httpd_start(&g_stream_server, &stream_config);
    if (err == ESP_OK) {
        err = register_handler(g_stream_server, "/stream", &stream_handler);
    }
    if (err != ESP_OK) {
        if (g_stream_server != nullptr) {
            httpd_stop(g_stream_server);
            g_stream_server = nullptr;
        }
        httpd_stop(g_control_server);
        g_control_server = nullptr;
        ESP_LOGE(kTag, "Could not start stream server: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(kTag, "Web UI: port 80; MJPEG stream: port 81");
    return ESP_OK;
}
