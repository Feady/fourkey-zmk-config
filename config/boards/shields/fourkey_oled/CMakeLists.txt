# Zephyr 会自动包含 shield 目录下的 CMakeLists.txt：
#   zephyr/boards/shields/CMakeLists.txt -> add_subdirectory(${SHIELD_DIRS}/...)
# 这里按 ZMK 官方 nice_view / englmaxi dongle_display 的写法，用 zephyr_library
# 提供强符号 zmk_display_status_screen()，覆盖 app/src/display/main.c 的 weak 实现。
if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  zephyr_library()
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)
endif()
