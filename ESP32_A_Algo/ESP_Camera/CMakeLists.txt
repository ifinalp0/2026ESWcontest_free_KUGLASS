cmake_minimum_required(VERSION 3.16)

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
project(ov2640_web_camera)
