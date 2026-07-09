#===============================================================================
# @brief    paibing 共用：启动、IMU、主循环
#===============================================================================
set(COMPONENT_NAME "paibing_common")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/startup.S
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/paibing_imu.c
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(WHOLE_LINK true)
set(BUILD_AS_OBJ false)
set(MAIN_COMPONENT false)

build_component()
