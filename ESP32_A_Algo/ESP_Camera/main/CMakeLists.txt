idf_component_register(
    SRCS
        "app_main.cpp"
        "camera_service.cpp"
        "serial_frame_server.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        espressif__esp32-camera
        esp_driver_uart
        esp_rom
        esp_system
        freertos
        heap
        log
)
