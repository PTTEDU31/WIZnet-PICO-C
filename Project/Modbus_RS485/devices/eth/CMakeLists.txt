set(WEB_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/tools/embed_web.py")
set(WEB_OUTPUT "${CMAKE_CURRENT_LIST_DIR}/web_assets.c")
set(WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/web")



add_custom_command(
    OUTPUT ${WEB_OUTPUT}
    COMMAND ${Python3_EXECUTABLE} ${WEB_SCRIPT}
    DEPENDS ${WEB_SCRIPT}
            # ${WEB_DIR}/index.html
            # ${WEB_DIR}/style.css
            # ${WEB_DIR}/script.js
    COMMENT "Embedding web files -> web_assets.c"
    VERBATIM
)


target_sources(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/dev_eth.c
    ${CMAKE_CURRENT_LIST_DIR}/dev_web.c
    ${CMAKE_CURRENT_LIST_DIR}/httpServer_user.c
    ${CMAKE_CURRENT_LIST_DIR}/snmp_custom.c
    ${WEB_OUTPUT}


    ${CMAKE_CURRENT_LIST_DIR}/httpServer/httpServer.c
    ${CMAKE_CURRENT_LIST_DIR}/httpServer/httpParser.c
    ${CMAKE_CURRENT_LIST_DIR}/httpServer/httpUtil.c

    ${CMAKE_CURRENT_LIST_DIR}/SNMP/snmp.c
    
)


target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/httpServer
    ${CMAKE_CURRENT_LIST_DIR}/SNMP

)

