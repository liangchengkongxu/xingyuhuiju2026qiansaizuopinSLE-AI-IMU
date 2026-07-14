#===============================================================================
# @brief    拍柄蓝牙版：BLE UART GATT 连接后 Notify 九轴
#===============================================================================
set(COMPONENT_NAME "paibing_ble_porting")

set(BLE_UART_SAMPLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../samples/products/ble_uart/ble_uart_server")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_ble_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_ble_policy.c
    ${BLE_UART_SAMPLE_DIR}/ble_uart_server.c
    ${BLE_UART_SAMPLE_DIR}/ble_uart_server_adv.c
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../version
    ${BLE_UART_SAMPLE_DIR}
)

set(PUBLIC_DEFINES
    CONFIG_BLE_UART_BUS=0
)

set(WHOLE_LINK true)
set(BUILD_AS_OBJ false)
set(MAIN_COMPONENT true)

build_component()
