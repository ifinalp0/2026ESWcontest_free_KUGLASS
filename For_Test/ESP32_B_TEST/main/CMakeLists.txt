idf_component_register(
    SRCS
        "analog_monitor.cpp"
        "app_main.cpp"
        "channel_manager.cpp"
        "control_protocol.cpp"
        "fault_manager.cpp"
        "protocol.cpp"
        "spwm_generator.cpp"
        "status_reporter.cpp"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        esp_adc
        esp_driver_gpio
        esp_driver_uart
        esp_driver_mcpwm
        esp_system
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
