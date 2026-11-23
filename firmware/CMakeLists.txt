cmake_minimum_required(VERSION 3.13)
if(NOT PICO_BOARD)
  set(PICO_BOARD pico_w)   # Raspberry Pi Pico W (RP2040); use -DPICO_BOARD=pico2_w for a Pico 2 W
endif()
include(pico_sdk_import.cmake)
project(trackpad_bt C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
add_executable(trackpad_bt main.c usb_descriptors.c bt.c ble.c bt_scelta.c)
target_include_directories(trackpad_bt PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(trackpad_bt PRIVATE pico_stdlib hardware_spi hardware_adc pico_unique_id
    tinyusb_device tinyusb_board
    pico_cyw43_arch_poll pico_btstack_cyw43 pico_btstack_classic pico_btstack_ble)
pico_btstack_make_gatt_header(trackpad_bt PRIVATE "${CMAKE_CURRENT_LIST_DIR}/trackpad.gatt")
target_compile_definitions(trackpad_bt PRIVATE CYW43_LWIP=0)
pico_add_extra_outputs(trackpad_bt)
