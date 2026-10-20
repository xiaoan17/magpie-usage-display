cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(magpie_mosaico VERSION 0.1.0)

# LVGL's Kconfig exposes no symbol for LV_TICK_CUSTOM_SYS_TIME_EXPR, and its
# fallback (millis()) is Arduino-only. Feed it esp_timer for this IDF build.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_definitions(${lvgl_lib} PRIVATE
    "LV_TICK_CUSTOM_SYS_TIME_EXPR=((uint32_t)(esp_timer_get_time() / 1000))")
