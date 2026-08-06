idf_component_register(
    SRCS
        "app_main.cpp"
        "camera_service.cpp"
        "serial_frame_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_uart
        esp_psram
)
