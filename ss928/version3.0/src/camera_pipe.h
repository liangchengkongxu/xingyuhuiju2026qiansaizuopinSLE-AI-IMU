/*
 * camera_pipe.h — VI/VPSS/VO 预览管道（version2.0）
 * 实现与 sample_vio 单路预览一致顺序，供 vo_gfbg_init 在启动 Qt 前调用。
 */
#ifndef CAMERA_PIPE_H
#define CAMERA_PIPE_H

#include "ot_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 未传 --mipi / WIDGET_CAM_MIPI 时的默认插座（与板厂固件 sample_vio 的 index 一致）：
 * HiEuerPI 等板常只接第二路 FPC，固件上为 sample_vio 1 0（sensor1），故默认 1。
 * 编译单 MIPI0 板：-DCAMERA_PIPE_DEFAULT_MIPI_SOCKET=0
 */
#ifndef CAMERA_PIPE_DEFAULT_MIPI_SOCKET
#define CAMERA_PIPE_DEFAULT_MIPI_SOCKET 1
#endif

/*
 * 选择 MIPI 插座（双插座底板）：须在 camera_pipe_mpp_prepare 之前调用。
 * 0 — sensor0：VI dev0 / pipe0 / VPSS0（固件 sample_vio 0 0）
 * 1 — sensor1：默认 VI dev0 / pipe0 / VPSS0，**sns bus 与默认单路相同（多为 i2c2）**；`WIDGET_CAM_SENSOR1_BUS=5` 强制 i2c5；`WIDGET_CAM_SENSOR1_STYLE=dev2` 走 dev2+MIPI2；`WIDGET_CAM_SENSOR1_SNS_AUX=0` 则 sns 0/0。
 * 2 — 备选：VI dev2 / pipe1 / VPSS1（SDK two_sensor）
 * 环境变量 WIDGET_CAM_I2C_BUS 可覆盖 sns bus_id（0～15）。
 */
td_void camera_pipe_set_mipi_socket(td_u32 socket_idx);

/* VB + sys + VI/VPSS 模式；需在其它 MPI 前调用，替代裸 ss_mpi_sys_init */
td_s32 camera_pipe_mpp_prepare(td_void);

/* 启 VI → VPSS → sample VO(HDMI) → VPSS bind VO；当前为全屏预览 */
td_s32 camera_pipe_preview_start(td_void);

/*
 * 调整 VO 通道显示矩形（与 Qt linuxfb 叠画：摄像头区域需 UI 背景透明）。
 * visible=TD_FALSE：关闭 VO 通道（离开摄像头页时调用）。
 */
td_s32 camera_pipe_vo_set_window(td_u32 x, td_u32 y, td_u32 width, td_u32 height, td_bool visible);

/* 逆序停止并 sample_comm_sys_exit */
td_void camera_pipe_shutdown(td_void);

/* version3.0：AI attach 用的 VPSS 目标（分类模型 224×224 NV12） */
#define CAMERA_PIPE_AI_VPSS_CHN 1
#define CAMERA_PIPE_AI_WIDTH    224
#define CAMERA_PIPE_AI_HEIGHT   224
/* 人体检测框：YOLOv8n COCO person，640×640 */
#define CAMERA_PIPE_DET_VPSS_CHN 2
#define CAMERA_PIPE_DET_WIDTH    640
#define CAMERA_PIPE_DET_HEIGHT   640

td_u32 camera_pipe_vpss_grp(td_void);
td_u32 camera_pipe_ai_vpss_chn(td_void);
td_u32 camera_pipe_det_vpss_chn(td_void);
td_bool camera_pipe_ai_channel_active(td_void);
td_bool camera_pipe_preview_active(td_void);
td_s32 camera_pipe_wait_ai_chn_ready(td_u32 max_sec);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_PIPE_H */
