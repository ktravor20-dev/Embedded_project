cmake_minimum_required(VERSION 3.16)
# Keep the original ESP32 configuration intact; use a separate S3 configuration.
set(IDF_TARGET "esp32s3" CACHE STRING "Smart CoCIS target")
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.s3" CACHE FILEPATH "S3 configuration")
set(EXTRA_COMPONENT_DIRS "src")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Embedded_Project)
