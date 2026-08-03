set(KUGLASS_SHARED_PROTOCOL_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../ESP32_A_Algo/main"
)

idf_component_register(
    SRCS
        "app_main.cpp"
        "channel_manager.cpp"
        "fault_manager.cpp"
        "spwm_generator.cpp"
        "status_reporter.cpp"
        "${KUGLASS_SHARED_PROTOCOL_DIR}/protocol.cpp"
    INCLUDE_DIRS
        "."
        "${KUGLASS_SHARED_PROTOCOL_DIR}"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_uart
        esp_driver_mcpwm
        esp_timer
)

foreach(KUGLASS_NUMERIC_OPTION
        KUGLASS_A_UART_TX_GPIO
        KUGLASS_A_UART_RX_GPIO)
    if(DEFINED ${KUGLASS_NUMERIC_OPTION})
        target_compile_definitions(
            ${COMPONENT_LIB}
            PRIVATE ${KUGLASS_NUMERIC_OPTION}=${${KUGLASS_NUMERIC_OPTION}}
        )
    endif()
endforeach()
