
set(TARGET_NAME Modbudrs485)


add_compile_options(-fno-stack-protector)

set(BUILD_SHARED_LIBS OFF)
set(CJSON_BUILD_SHARED_LIBS OFF)
set(CJSON_BUILD_TEST OFF)
set(CJSON_WITHOUT_TESTS ON)
set(CJSON_BUILD_DOCS OFF)
set(ENABLE_CJSON_TEST OFF)


# set(PROJECT_NAME ${TARGET_NAME})
add_executable(${TARGET_NAME}
        main.c
        )

add_subdirectory(lib/cJSON-master)
add_subdirectory(devices/eth)
add_subdirectory(devices/modbus)
add_subdirectory(devices/psu_data)
add_subdirectory(devices/led_status)
add_subdirectory(devices/config)



target_link_libraries(${TARGET_NAME}
        pico_stdlib
        hardware_pwm
        hardware_spi
        hardware_dma
        hardware_pio
        hardware_flash
        ETHERNET_FILES
        IOLIBRARY_FILES
        DHCP_FILES
        PORT_COMMON_FILES
        LOOPBACK_FILES
        # TIMER_FILES    
        SNTP_FILES
        pico_multicore
        cjson
        )

target_include_directories(${TARGET_NAME} PRIVATE
#     ../include
        lib/cJSON-master
        devices
)


pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/led_status.pio)

pico_enable_stdio_usb(${TARGET_NAME} 0)
pico_enable_stdio_uart(${TARGET_NAME} 1)

pico_add_extra_outputs(${TARGET_NAME})
