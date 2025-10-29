
set(TARGET_NAME Modbudrs485)

# set(PROJECT_NAME ${TARGET_NAME})
add_executable(${TARGET_NAME}
        main.c
        )

target_link_libraries(${TARGET_NAME}
        pico_stdlib
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
        app_config
        )


target_include_directories(${TARGET_NAME} PRIVATE
#     ../include
)


# Ethernet
add_subdirectory(devices/eth)
add_subdirectory(devices/modbus)
add_subdirectory(config)

pico_enable_stdio_usb(${TARGET_NAME} 0)
pico_enable_stdio_uart(${TARGET_NAME} 1)

pico_add_extra_outputs(${TARGET_NAME})

# # # --- Ghi ra file để VSCode đọc ---
# set(EXEC_PATH "${CMAKE_BINARY_DIR}/Project/${TARGET_NAME}/${TARGET_NAME}.elf")
# file(WRITE "${CMAKE_BINARY_DIR}/current_target_path.txt" "${EXEC_PATH}")
# set(EXEC_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}.elf")
# file(WRITE "${CMAKE_BINARY_DIR}/current_target_path.txt" "${EXEC_PATH}")
