cmake_minimum_required(VERSION 3.22)

# This project is dedicated to ESP32-S3-DevKitC-1U-N8R8. A command-line
# -DIDF_TARGET value can still be supplied, but configuration rejects
# non-ESP32-S3 targets. Memory-profile checks live in camera_pins.h.
set(IDF_TARGET "esp32s3" CACHE STRING "ESP32-S3-DevKitC-1U-N8R8 target")
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR
        "ESP_Camera supports only ESP32-S3-DevKitC-1U-N8R8 "
        "(IDF_TARGET=esp32s3)")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only the components required by the wired camera application and its
# dependency graph. This keeps unused networking and Bluetooth stacks out of
# configuration and compilation.
idf_build_set_property(MINIMAL_BUILD ON)
project(ov2640_serial_camera)
