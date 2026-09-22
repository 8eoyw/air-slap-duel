# Standalone referee app: flash onto the XiaoZhi board (or any ESP32-S3 devkit)
# WITHOUT the xiaozhi firmware to validate BLE central + two Nanos in weeks 1-2,
# and to develop the referee before touching xiaozhi-esp32.
#
#   idf.py set-target esp32s3 && idf.py build flash monitor
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(slap_referee_standalone)
