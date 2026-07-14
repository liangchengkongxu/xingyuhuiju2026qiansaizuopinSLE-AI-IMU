#===============================================================================
# @brief    paibing：common + 星闪版(sle) 或 蓝牙版(ble)，由编译 target 选择
#===============================================================================
add_subdirectory(common)

if("${TARGET_COMMAND}" STREQUAL "standard-bs20-n1200-ble")
    add_subdirectory(ble)
else()
    add_subdirectory(sle)
endif()
