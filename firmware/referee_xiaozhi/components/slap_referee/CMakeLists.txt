# The portable referee core lives in the Arduino library so the Nano hub and the
# host tests share it. Override SLAP_COMMON_DIR if you copy this component elsewhere
# (e.g. into the xiaozhi-esp32 tree).
if(NOT DEFINED SLAP_COMMON_DIR)
    set(SLAP_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/SlapCommon/src")
endif()

idf_component_register(
    SRCS
        "slap_referee.cc"
        "slap_ble_central.cc"
        "slap_uart_bridge.cc"
        "slap_servo.cc"
        "${SLAP_COMMON_DIR}/referee_core.cpp"
    INCLUDE_DIRS "include" "${SLAP_COMMON_DIR}"
    REQUIRES bt esp_timer esp_driver_uart esp_driver_ledc json
)
