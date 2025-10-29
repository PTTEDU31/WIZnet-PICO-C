# Add system configuration module
add_library(app_config
    app_config.c
    app_config.h
)

target_include_directories(app_config PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(app_config
    pico_stdlib
    hardware_flash
    hardware_sync
)
