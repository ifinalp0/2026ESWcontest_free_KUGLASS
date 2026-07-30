idf_component_register(
    SRCS
        "app_main.cpp"
        "protocol.cpp"
        "channel_manager.cpp"
        "spwm_generator.cpp"
        "fault_manager.cpp"
        "telemetry_reporter.cpp"
        "adc_sampler.cpp"
    INCLUDE_DIRS "."
)

if(DEFINED KUGLASS_FIRST_CHANNEL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KUGLASS_FIRST_CHANNEL=${KUGLASS_FIRST_CHANNEL})
endif()

if(DEFINED KUGLASS_CONTROLLER_ID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KUGLASS_CONTROLLER_ID="${KUGLASS_CONTROLLER_ID}")
endif()
