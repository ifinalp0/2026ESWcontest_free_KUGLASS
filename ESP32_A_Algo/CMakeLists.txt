cmake_minimum_required(VERSION 3.16)

# A clean checkout must select the wired ESP32-S3 profile without consulting
# a sibling project or a pre-existing sdkconfig.
set(IDF_TARGET "esp32s3" CACHE STRING "ESP32_A target")
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "ESP32_A supports only IDF_TARGET=esp32s3")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kuglass_esp32_algo)
