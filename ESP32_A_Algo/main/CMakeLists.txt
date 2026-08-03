set(KUGLASS_CAMERA_SERVICE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../ESP_Camera/main"
)

idf_component_register(
    SRCS
        "app_main.cpp"
        "protocol.cpp"
        "ui_protocol.cpp"
        "policy_engine.cpp"
        "camera_metric_adapter.cpp"
        "downstream_status.cpp"
        "ds18b20_sensor.cpp"
        "esp32_b_link.cpp"
        "server_console.cpp"
        "master_telemetry.cpp"
        "${KUGLASS_CAMERA_SERVICE_DIR}/camera_service.cpp"
    INCLUDE_DIRS
        "."
        "${KUGLASS_CAMERA_SERVICE_DIR}"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
        esp_psram
        esp_timer
)

foreach(KUGLASS_NUMERIC_OPTION
        KUGLASS_ONBOARD_CAMERA
        KUGLASS_ALLOW_DIAGNOSTIC_COMMANDS
        KUGLASS_B_UART_TX_GPIO
        KUGLASS_B_UART_RX_GPIO
        KUGLASS_DS18B20_GPIO
        KUGLASS_B_SUPPORTS_RESET_FAULT)
    if(DEFINED ${KUGLASS_NUMERIC_OPTION})
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE ${KUGLASS_NUMERIC_OPTION}=${${KUGLASS_NUMERIC_OPTION}}
        )
    endif()
endforeach()
