target_sources(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/led_status.c
)

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}                # include chính trong modbus/
    ${CMAKE_CURRENT_LIST_DIR}/../../config   # include thư mục config/
)
