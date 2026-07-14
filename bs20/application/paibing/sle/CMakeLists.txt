#===============================================================================
# @brief    拍柄星闪版
#===============================================================================
set(COMPONENT_NAME "paibing_sle_porting")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_sle_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_sle_server_adv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_multi_mode.c
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../version
)

set(PUBLIC_DEFINES
    CONFIG_BLE_UART_BUS=0
)

set(WHOLE_LINK true)
set(BUILD_AS_OBJ false)
set(MAIN_COMPONENT true)

build_component()
