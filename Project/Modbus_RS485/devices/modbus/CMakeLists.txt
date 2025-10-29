target_sources(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/dev_modbus.c
)

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}                # include chính trong modbus/
    ${CMAKE_CURRENT_LIST_DIR}/../../config   # include thư mục config/
)
