/*
 * camera_pipe.c — OS08A20: VB + VI + VPSS + VO(HDMI) 全屏预览
 * EULER_2R + OS08A20 对齐 Gitee hieulerpi/SS928V100_SDK_V2.0.2.2_MPP_Sample 中 sample_vi_get_one_sensor_vi_cfg()：
 * J3 sensor0 -> i2c5 clk0 + VI dev0；J4 sensor1 -> i2c7 clk1 + VI dev2 + pipe0。
 * socket 2：仅第二插座（two_sensor 第二路）-> VI dev2 + pipe1 + i2c7。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera_pipe.h"
#include "sample_comm.h"
#include "securec.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_isp.h"
#include "ot_defines.h"

#define LOG(fmt, ...) fprintf(stderr, "[camera_pipe] " fmt "\n", ##__VA_ARGS__)

/* Euler sample_vio_one_sensor：VB_YUV_ROUTE_CNT + VB_RAW_CNT_NONE(0) */
#define VB_YUV_SOCKET0  10
#define VB_RAW_SOCKET0  0
#define VB_YUV_SOCKET1  15
#define VB_RAW_SOCKET1  8
#define VB_SUPP         OT_VB_SUPPLEMENT_BNR_MOT_MASK

static sample_vi_cfg g_vi_cfg;
static sample_vo_cfg g_vo_cfg;
static td_bool g_mpp_ready = TD_FALSE;
static td_bool g_preview_on = TD_FALSE;

static td_u32 g_mipi_socket; /* 0 或 1，未调用 set 前须为 0 */
static ot_vi_pipe  g_vi_pipe  = 0;
static ot_vi_chn   g_vi_chn   = 0;
static ot_vpss_grp g_vpss_grp = 0;
static const ot_vpss_chn g_vpss_chn = 0;
static const ot_vpss_chn g_vpss_ai_chn = CAMERA_PIPE_AI_VPSS_CHN;
static const ot_vo_layer g_vo_layer = 0;
static const ot_vo_chn   g_vo_chn   = 0;

static td_u32 g_vo_win_x = 0xffffffffU;
static td_u32 g_vo_win_y = 0xffffffffU;
static td_u32 g_vo_win_w = 0xffffffffU;
static td_u32 g_vo_win_h = 0xffffffffU;
static td_bool g_vo_ui_shown = TD_FALSE;
static td_bool g_vo_pip_hide = TD_FALSE;

/* 内核里可能残留 ISP（进程被 kill/timeout 打断时常见），会报 ISP[0] already inited */
static td_void camera_fill_vpss_chn_enable(td_bool *chn_enable, td_u32 arr_size);

static td_void camera_try_isp_exit_all_pipes(td_void)
{
    ot_vi_pipe p;
    for (p = 0; p < OT_VI_MAX_PHYS_PIPE_NUM; p++) {
        (td_void)ss_mpi_isp_exit(p);
    }
}

/* 上次异常退出后 VI/VPSS/VO 可能半残留，先尽力清再启预览 */
static td_void camera_force_stale_preview_cleanup(td_void)
{
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM];

    camera_fill_vpss_chn_enable(chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
    (void)sample_comm_vpss_un_bind_vo(g_vpss_grp, g_vpss_chn, g_vo_layer, g_vo_chn);
    (void)sample_comm_vo_stop_vo(&g_vo_cfg);
    (void)sample_common_vpss_stop(g_vpss_grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
    (void)sample_comm_vi_un_bind_vpss(g_vi_pipe, g_vi_chn, g_vpss_grp, g_vpss_chn);
    (void)sample_comm_vi_stop_vi(&g_vi_cfg);
    camera_try_isp_exit_all_pipes();
    g_preview_on = TD_FALSE;
    usleep(150000);
}

td_void camera_pipe_set_mipi_socket(td_u32 socket_idx)
{
    /* 0: dev0/pipe0；1: sensor1 默认 dev0+默认 sns bus；2: dev2/pipe1（SDK two_sensor） */
    if (socket_idx >= 3U) {
        socket_idx = 2U;
    }
    g_mipi_socket = socket_idx;
    if (g_mipi_socket == 0U) {
        g_vi_pipe = 0;
        g_vpss_grp = 0;
    } else if (g_mipi_socket == 1U) {
        g_vi_pipe = 0;
        g_vpss_grp = 0;
    } else {
        g_vi_pipe = 1;
        g_vpss_grp = 1;
    }
    LOG("MIPI socket set to %u (vi_pipe=%d vpss_grp=%d)", g_mipi_socket, (int)g_vi_pipe, (int)g_vpss_grp);
}

static td_void camera_fill_vb_cfg(ot_size *size, ot_vb_cfg *vb_cfg, ot_vi_video_mode video_mode,
    td_u32 yuv_cnt, td_u32 raw_cnt)
{
    ot_vb_calc_cfg calc_cfg;
    ot_pic_buf_attr buf_attr;

    (td_void)memset_s(vb_cfg, sizeof(ot_vb_cfg), 0, sizeof(ot_vb_cfg));
    vb_cfg->max_pool_cnt = 128;

    buf_attr.width         = size->width;
    buf_attr.height        = size->height;
    buf_attr.align         = OT_DEFAULT_ALIGN;
    buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
    buf_attr.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    buf_attr.compress_mode = OT_COMPRESS_MODE_SEG;
    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
    vb_cfg->common_pool[0].blk_size = calc_cfg.vb_size;
    vb_cfg->common_pool[0].blk_cnt  = yuv_cnt;

    buf_attr.pixel_format  = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
    buf_attr.compress_mode = (video_mode == OT_VI_VIDEO_MODE_NORM) ? OT_COMPRESS_MODE_LINE : OT_COMPRESS_MODE_NONE;
    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
    vb_cfg->common_pool[1].blk_size = calc_cfg.vb_size;
    vb_cfg->common_pool[1].blk_cnt  = raw_cnt;
}

/* 对齐 sample_vio.c sample_vi_get_two_sensor_vi_cfg() 中 vi_cfg1，用于仅接第二插座 */
static td_void camera_apply_second_mipi_vi_cfg(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 2;   /* dev2 for sensor on second connector */
    const ot_vi_pipe vi_pipe = 1; /* dev2 bind pipe1 */

    vi_cfg->sns_info.bus_id = 7; /* Euler two_sensor 第二路 J4：i2c7 */
    vi_cfg->sns_info.sns_clk_src = 1;
    vi_cfg->sns_info.sns_rst_src = 1;

    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, vi_dev, &vi_cfg->mipi_info);
    vi_cfg->mipi_info.divide_mode = LANE_DIVIDE_MODE_1; /* 双 MIPI 底板与 sample two_sensor 一致 */
    vi_cfg->dev_info.vi_dev = vi_dev;

    vi_cfg->bind_pipe.pipe_num = 1;
    vi_cfg->bind_pipe.pipe_id[0] = vi_pipe;

    /* grp 的 wdr/cache_line 已由 get_default_vi_cfg 填好，仅改 fusion 组号与 pipe 映射（对齐 two_sensor vi_cfg1） */
    vi_cfg->grp_info.grp_num = 1;
    vi_cfg->grp_info.fusion_grp[0] = 1;
    vi_cfg->grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;

    sample_comm_vi_get_default_pipe_info(SENSOR0_TYPE, &vi_cfg->bind_pipe, vi_cfg->pipe_info);
}

/* EULER_2R J3 sensor0（单路 4lane） */
static td_void camera_apply_euler_2r_one_sensor0(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 0;
    const ot_vi_pipe vi_pipe = 0;

    vi_cfg->mipi_info.divide_mode = LANE_DIVIDE_MODE_1;
    vi_cfg->sns_info.bus_id = 5; /* i2c5 */
    vi_cfg->sns_info.sns_clk_src = 0;
    vi_cfg->sns_info.sns_rst_src = 0;

    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, vi_dev, &vi_cfg->mipi_info);
    vi_cfg->dev_info.vi_dev = vi_dev;
    vi_cfg->bind_pipe.pipe_num = 1;
    vi_cfg->bind_pipe.pipe_id[0] = vi_pipe;
    vi_cfg->grp_info.grp_num = 1;
    vi_cfg->grp_info.fusion_grp[0] = 0;
    vi_cfg->grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;
}

/* EULER_2R J4 sensor1（单路 4lane）：VI dev2 + pipe0 + i2c7 */
static td_void camera_apply_euler_2r_one_sensor1(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 2;
    const ot_vi_pipe vi_pipe = 0;

    vi_cfg->mipi_info.divide_mode = LANE_DIVIDE_MODE_1;
    vi_cfg->sns_info.bus_id = 7; /* i2c7 */
    vi_cfg->sns_info.sns_clk_src = 1;
    vi_cfg->sns_info.sns_rst_src = 1;

    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, vi_dev, &vi_cfg->mipi_info);
    vi_cfg->dev_info.vi_dev = vi_dev;
    vi_cfg->bind_pipe.pipe_num = 1;
    vi_cfg->bind_pipe.pipe_id[0] = vi_pipe;
    vi_cfg->grp_info.grp_num = 1;
    vi_cfg->grp_info.fusion_grp[0] = 0;
    vi_cfg->grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;
}

static td_void camera_apply_sensor1_env_overrides(sample_vi_cfg *vi_cfg)
{
    const char *aux;
    const char *b;

    b = getenv("WIDGET_CAM_SENSOR1_BUS");
    if (b != NULL && b[0] != '\0') {
        td_u32 bus = (td_u32)strtoul(b, NULL, 0);
        if (bus <= 15U) {
            vi_cfg->sns_info.bus_id = (td_u8)bus;
            LOG("sensor1: WIDGET_CAM_SENSOR1_BUS=%u", (unsigned)bus);
        }
    }

    aux = getenv("WIDGET_CAM_SENSOR1_SNS_AUX");
    if (aux != NULL && aux[0] == '0') {
        vi_cfg->sns_info.sns_clk_src = 0;
        vi_cfg->sns_info.sns_rst_src = 0;
        LOG("sensor1: WIDGET_CAM_SENSOR1_SNS_AUX=0 -> sns_clk/rst 0/0");
    }
}

/* 与 Euler 单路 sensor1 等价；保留宏开关供脚本显式指定 */
static td_void camera_apply_dev2_pipe0_vi_cfg(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 2;
    const ot_vi_pipe vi_pipe = 0;

    vi_cfg->sns_info.bus_id = 7; /* EULER_2R J4：i2c7 */
    vi_cfg->sns_info.sns_clk_src = 1;
    vi_cfg->sns_info.sns_rst_src = 1;

    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, vi_dev, &vi_cfg->mipi_info);
    vi_cfg->dev_info.vi_dev = vi_dev;

    vi_cfg->bind_pipe.pipe_num = 1;
    vi_cfg->bind_pipe.pipe_id[0] = vi_pipe;

    vi_cfg->grp_info.grp_num = 1;
    vi_cfg->grp_info.fusion_grp[0] = 0;
    vi_cfg->grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;

    sample_comm_vi_get_default_pipe_info(SENSOR0_TYPE, &vi_cfg->bind_pipe, vi_cfg->pipe_info);
}

static td_void camera_apply_i2c_bus_env(sample_vi_cfg *vi_cfg)
{
    const char *bus_env = getenv("WIDGET_CAM_I2C_BUS");
    if (bus_env == NULL || bus_env[0] == '\0') {
        return;
    }
    td_u32 bus = (td_u32)strtoul(bus_env, NULL, 0);
    if (bus > 15U) {
        return;
    }
    vi_cfg->sns_info.bus_id = (td_u8)bus;
    LOG("sns_info.bus_id overridden by WIDGET_CAM_I2C_BUS=%u", (unsigned)bus);
}

static td_bool camera_ai_channel_wanted(td_void)
{
    const char *dis = getenv("WIDGET_AI_DISABLE");
    if (dis != NULL && dis[0] != '\0' && dis[0] != '0') {
        return TD_FALSE;
    }
    return TD_TRUE;
}

static td_bool camera_pose_ch1_only(td_void)
{
    const char *v = getenv("WIDGET_POSE_CH1_ONLY");
    if (v != NULL && v[0] == '1') {
        return TD_TRUE;
    }
    return TD_FALSE;
}

static td_void camera_fill_vpss_chn_enable(td_bool *chn_enable, td_u32 arr_size)
{
    td_u32 i;
    for (i = 0; i < arr_size; ++i) {
        chn_enable[i] = TD_FALSE;
    }
    chn_enable[0] = TD_TRUE;
    if (camera_ai_channel_wanted()) {
        chn_enable[CAMERA_PIPE_AI_VPSS_CHN] = TD_TRUE;
        /* Pose 用 ch1 224 软件放大即可，关掉 ch2 640 可显著减轻 VPSS 缓冲压力 */
        if (camera_pose_ch1_only() != TD_TRUE) {
            chn_enable[CAMERA_PIPE_DET_VPSS_CHN] = TD_TRUE;
        } else {
            LOG("VPSS ch2 disabled (WIDGET_POSE_CH1_ONLY=1, pose uses ch1)");
        }
    }
}

td_s32 camera_pipe_mpp_prepare(td_void)
{
    td_s32 ret;
    ot_size size;
    ot_vb_cfg vb_cfg;
    ot_vi_vpss_mode_type mode_type;
    ot_vi_video_mode video_mode = OT_VI_VIDEO_MODE_NORM;
    td_u32 yuv_cnt, raw_cnt;

    if (g_mpp_ready == TD_TRUE) {
        return TD_SUCCESS;
    }

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &size);

    if (g_mipi_socket == 0U) {
        mode_type = OT_VI_ONLINE_VPSS_OFFLINE;
        yuv_cnt = VB_YUV_SOCKET0;
        raw_cnt = VB_RAW_SOCKET0;
        if (camera_ai_channel_wanted()) {
            yuv_cnt += 12U;
        }
        LOG("MPP: socket0 VI_ONLINE_VPSS_OFFLINE, VB yuv=%u raw=%u", yuv_cnt, raw_cnt);
    } else if (g_mipi_socket == 1U) {
        /* 固件单路 sensor1 与 sensor0 同为 vi_pipe:0，常用 Online 模式（与 0 0 同类 VB） */
        mode_type = OT_VI_ONLINE_VPSS_OFFLINE;
        yuv_cnt = VB_YUV_SOCKET0;
        raw_cnt = VB_RAW_SOCKET0;
        if (camera_ai_channel_wanted()) {
            yuv_cnt += 12U;
        }
        LOG("MPP: socket1 VI_ONLINE_VPSS_OFFLINE, VB yuv=%u raw=%u (firmware sensor1)", yuv_cnt, raw_cnt);
    } else {
        mode_type = OT_VI_OFFLINE_VPSS_OFFLINE;
        yuv_cnt = VB_YUV_SOCKET1;
        raw_cnt = VB_RAW_SOCKET1;
        LOG("MPP: socket%u VI_OFFLINE_VPSS_OFFLINE, VB yuv=%u raw=%u", g_mipi_socket, yuv_cnt, raw_cnt);
    }

    camera_fill_vb_cfg(&size, &vb_cfg, video_mode, yuv_cnt, raw_cnt);

    ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, VB_SUPP);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_sys_init_with_vb_supplement failed, ret=%#x", ret);
        return ret;
    }

    {
        /* 默认清理残留 ISP，避免 kill 后重启报 ISP already inited */
        const char *isp_clean = getenv("WIDGET_CAM_ISP_CLEAN");
        if (isp_clean == NULL || isp_clean[0] == '\0' || isp_clean[0] != '0') {
            camera_try_isp_exit_all_pipes();
            LOG("isp_exit all pipes before VI init");
        }
    }

    ret = sample_comm_vi_set_vi_vpss_mode(mode_type, video_mode);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_set_vi_vpss_mode failed, ret=%#x", ret);
        sample_comm_sys_exit();
        return ret;
    }

    g_mpp_ready = TD_TRUE;
    LOG("MPP(VB+VI/VPSS mode) ready");
    return TD_SUCCESS;
}

td_u32 camera_pipe_vpss_grp(td_void)
{
    return (td_u32)g_vpss_grp;
}

td_u32 camera_pipe_ai_vpss_chn(td_void)
{
    return (td_u32)g_vpss_ai_chn;
}

td_u32 camera_pipe_det_vpss_chn(td_void)
{
    return (td_u32)CAMERA_PIPE_DET_VPSS_CHN;
}

td_bool camera_pipe_ai_channel_active(td_void)
{
    return camera_ai_channel_wanted();
}

td_bool camera_pipe_preview_active(td_void)
{
    return g_preview_on;
}

td_s32 camera_pipe_wait_ai_chn_ready(td_u32 max_sec)
{
    td_u32 i;
    td_u32 tries;
    ot_video_frame_info frame;

    if (g_preview_on != TD_TRUE || camera_ai_channel_wanted() != TD_TRUE) {
        return TD_SUCCESS;
    }
    tries = max_sec * 5U;
    if (tries < 20U) {
        tries = 20U;
    }

    for (i = 0; i < tries; i++) {
        td_s32 ret;
        (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        ret = ss_mpi_vpss_get_chn_frame(g_vpss_grp, g_vpss_ai_chn, &frame, 500);
        if (ret == TD_SUCCESS) {
            (td_void)ss_mpi_vpss_release_chn_frame(g_vpss_grp, g_vpss_ai_chn, &frame);
            LOG("AI VPSS chn%u first frame ready after %u tries", (unsigned)g_vpss_ai_chn, i + 1U);
            return TD_SUCCESS;
        }
        usleep(200000);
    }

    LOG("AI VPSS chn%u not ready within %us (attach may retry)", (unsigned)g_vpss_ai_chn, max_sec);
    return TD_FAILURE;
}

static td_s32 camera_start_vpss(ot_size *in_size)
{
    td_s32 ret;
    ot_low_delay_info low_delay_info;
    ot_vpss_grp_attr grp_attr;
    ot_vpss_chn_attr chn_attr[OT_VPSS_MAX_PHYS_CHN_NUM];
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM];

    camera_fill_vpss_chn_enable(chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);

    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width  = in_size->width;
    grp_attr.max_height = in_size->height;

    sample_comm_vpss_get_default_chn_attr(&chn_attr[0]);
    chn_attr[0].width  = in_size->width;
    chn_attr[0].height = in_size->height;
    chn_attr[0].depth  = 2;

    if (chn_enable[CAMERA_PIPE_AI_VPSS_CHN] == TD_TRUE) {
        sample_comm_vpss_get_default_chn_attr(&chn_attr[CAMERA_PIPE_AI_VPSS_CHN]);
        chn_attr[CAMERA_PIPE_AI_VPSS_CHN].width         = CAMERA_PIPE_AI_WIDTH;
        chn_attr[CAMERA_PIPE_AI_VPSS_CHN].height        = CAMERA_PIPE_AI_HEIGHT;
        chn_attr[CAMERA_PIPE_AI_VPSS_CHN].compress_mode = OT_COMPRESS_MODE_NONE;
        chn_attr[CAMERA_PIPE_AI_VPSS_CHN].depth         = 8;
        chn_attr[CAMERA_PIPE_AI_VPSS_CHN].pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        LOG("VPSS AI chn%d: %ux%u NV12 depth=%d", CAMERA_PIPE_AI_VPSS_CHN,
            CAMERA_PIPE_AI_WIDTH, CAMERA_PIPE_AI_HEIGHT, chn_attr[CAMERA_PIPE_AI_VPSS_CHN].depth);
    }

    if (chn_enable[CAMERA_PIPE_DET_VPSS_CHN] == TD_TRUE) {
        sample_comm_vpss_get_default_chn_attr(&chn_attr[CAMERA_PIPE_DET_VPSS_CHN]);
        chn_attr[CAMERA_PIPE_DET_VPSS_CHN].width         = CAMERA_PIPE_DET_WIDTH;
        chn_attr[CAMERA_PIPE_DET_VPSS_CHN].height        = CAMERA_PIPE_DET_HEIGHT;
        chn_attr[CAMERA_PIPE_DET_VPSS_CHN].compress_mode = OT_COMPRESS_MODE_NONE;
        chn_attr[CAMERA_PIPE_DET_VPSS_CHN].depth         = 8;
        chn_attr[CAMERA_PIPE_DET_VPSS_CHN].pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        LOG("VPSS DET chn%d: %ux%u NV12 depth=%d", CAMERA_PIPE_DET_VPSS_CHN,
            CAMERA_PIPE_DET_WIDTH, CAMERA_PIPE_DET_HEIGHT, chn_attr[CAMERA_PIPE_DET_VPSS_CHN].depth);
    }

    ret = sample_common_vpss_start(g_vpss_grp, chn_enable, &grp_attr, chn_attr, OT_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != TD_SUCCESS) {
        LOG("sample_common_vpss_start failed, ret=%#x", ret);
        return ret;
    }

    low_delay_info.enable     = TD_TRUE;
    low_delay_info.line_cnt   = 200;
    low_delay_info.one_buf_en = TD_FALSE;
    ret = ss_mpi_vpss_set_low_delay_attr(g_vpss_grp, 0, &low_delay_info);
    if (ret != TD_SUCCESS) {
        LOG("ss_mpi_vpss_set_low_delay_attr failed, ret=%#x (non-fatal)", ret);
    }
    return TD_SUCCESS;
}

td_s32 camera_pipe_preview_start(td_void)
{
    td_s32 ret;
    ot_size in_size;
    td_u32 attempt;

    if (g_mpp_ready != TD_TRUE) {
        LOG("MPP not prepared");
        return TD_FAILURE;
    }
    if (g_preview_on == TD_TRUE) {
        return TD_SUCCESS;
    }

    camera_force_stale_preview_cleanup();

    for (attempt = 0; attempt < 2U; ++attempt) {
        if (attempt > 0U) {
            LOG("preview_start retry %u after stale cleanup", attempt);
            camera_force_stale_preview_cleanup();
        }

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_size);
    sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, &g_vi_cfg);

    if (g_mipi_socket == 0U) {
        camera_apply_euler_2r_one_sensor0(&g_vi_cfg);
        LOG("sensor0: EULER_2R J3 (VI dev0 + i2c5 + clk0)");
    } else if (g_mipi_socket == 1U) {
        const char *st = getenv("WIDGET_CAM_SENSOR1_STYLE");
        if (st != NULL && strcmp(st, "dev2") == 0) {
            camera_apply_dev2_pipe0_vi_cfg(&g_vi_cfg);
            camera_apply_sensor1_env_overrides(&g_vi_cfg);
            LOG("sensor1: WIDGET_CAM_SENSOR1_STYLE=dev2 (VI dev2 + pipe0 + i2c7)");
        } else {
            camera_apply_euler_2r_one_sensor1(&g_vi_cfg);
            camera_apply_sensor1_env_overrides(&g_vi_cfg);
            LOG("sensor1: EULER_2R J4 (VI dev2 + i2c7 + clk1)");
        }
    } else if (g_mipi_socket == 2U) {
        camera_apply_second_mipi_vi_cfg(&g_vi_cfg);
    }

    camera_apply_i2c_bus_env(&g_vi_cfg);

    /* 上次 kill 未走 shutdown 时 ISP 可能残留，先清再启 VI */
    camera_try_isp_exit_all_pipes();
    usleep(100000);

    ret = sample_comm_vi_start_vi(&g_vi_cfg);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_start_vi failed, ret=%#x — retry after isp_exit", ret);
        camera_try_isp_exit_all_pipes();
        usleep(200000);
        ret = sample_comm_vi_start_vi(&g_vi_cfg);
    }
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_start_vi failed, ret=%#x", ret);
        goto fail_vi;
    }

    ret = sample_comm_vi_bind_vpss(g_vi_pipe, g_vi_chn, g_vpss_grp, g_vpss_chn);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_bind_vpss failed, ret=%#x", ret);
        goto fail_vi;
    }

    ret = camera_start_vpss(&in_size);
    if (ret != TD_SUCCESS) {
        goto fail_bind;
    }

    (td_void)memset_s(&g_vo_cfg, sizeof(g_vo_cfg), 0, sizeof(g_vo_cfg));
    ret = sample_comm_vo_get_def_config(&g_vo_cfg);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vo_get_def_config failed, ret=%#x", ret);
        goto fail_vpss;
    }

    ret = sample_comm_vo_start_vo(&g_vo_cfg);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vo_start_vo failed, ret=%#x", ret);
        goto fail_vpss;
    }

    ret = sample_comm_vpss_bind_vo(g_vpss_grp, g_vpss_chn, g_vo_layer, g_vo_chn);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vpss_bind_vo failed, ret=%#x", ret);
        goto fail_vo;
    }

    g_preview_on = TD_TRUE;
    /* 勿 disable VO：保持 2x2 像素消费，否则 VPSS ch0 缓冲满会导致 ch1 AI 饿死 */
    (void)camera_pipe_vo_set_window(0U, 0U, 32U, 32U, TD_FALSE);
    LOG("VI+VPSS+VO preview started (VO pip-hidden until UI), mipi_socket=%u", g_mipi_socket);
    return TD_SUCCESS;

fail_vo:
    sample_comm_vo_stop_vo(&g_vo_cfg);
fail_vpss: {
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM];
    camera_fill_vpss_chn_enable(chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
    sample_common_vpss_stop(g_vpss_grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
}
fail_bind:
    sample_comm_vi_un_bind_vpss(g_vi_pipe, g_vi_chn, g_vpss_grp, g_vpss_chn);
fail_vi:
    sample_comm_vi_stop_vi(&g_vi_cfg);
    camera_try_isp_exit_all_pipes();
    g_preview_on = TD_FALSE;
    } /* for attempt */

    return TD_FAILURE;
}

td_s32 camera_pipe_vo_set_window(td_u32 x, td_u32 y, td_u32 width, td_u32 height, td_bool visible)
{
    td_s32 ret;
    ot_vo_chn_attr chn_attr;
    td_bool ui_show = visible;
    td_bool chn_on = TD_FALSE;

    if (g_preview_on != TD_TRUE) {
        return TD_FAILURE;
    }

    if (ui_show == TD_FALSE) {
        /* UI 隐藏：缩小到 32x32 像素并继续 enable，避免 VPSS/AI 通道饿死 */
        if (g_vo_pip_hide == TD_TRUE && g_vo_ui_shown == TD_FALSE) {
            return TD_SUCCESS;
        }
        x = 0U;
        y = 0U;
        width = 32U;
        height = 32U;
        g_vo_ui_shown = TD_FALSE;
        g_vo_pip_hide = TD_TRUE;
        chn_on = TD_TRUE;
    } else {
        if (width < 32U || height < 32U) {
            LOG("camera_pipe_vo_set_window: size too small %ux%u", (unsigned)width, (unsigned)height);
            return TD_FAILURE;
        }
        g_vo_ui_shown = TD_TRUE;
        g_vo_pip_hide = TD_FALSE;
        chn_on = TD_TRUE;
    }

    x = (td_u32)OT_ALIGN_DOWN((td_s32)x, 2);
    y = (td_u32)OT_ALIGN_DOWN((td_s32)y, 2);
    width = (td_u32)OT_ALIGN_DOWN((td_s32)width, 2);
    height = (td_u32)OT_ALIGN_DOWN((td_s32)height, 2);

    if (g_vo_pip_hide == TD_FALSE && g_vo_ui_shown == TD_TRUE &&
        x == g_vo_win_x && y == g_vo_win_y && width == g_vo_win_w && height == g_vo_win_h) {
        return TD_SUCCESS;
    }

    ret = ss_mpi_vo_get_chn_attr(g_vo_layer, g_vo_chn, &chn_attr);
    if (ret != TD_SUCCESS) {
        LOG("camera_pipe_vo_set_window: get_chn_attr failed, ret=%#x", ret);
        return ret;
    }

    chn_attr.rect.x = (td_s32)x;
    chn_attr.rect.y = (td_s32)y;
    chn_attr.rect.width = width;
    chn_attr.rect.height = height;

    ret = ss_mpi_vo_set_chn_attr(g_vo_layer, g_vo_chn, &chn_attr);
    if (ret != TD_SUCCESS) {
        LOG("camera_pipe_vo_set_window: set_chn_attr failed, ret=%#x", ret);
        return ret;
    }
    if (chn_on == TD_TRUE) {
        ret = ss_mpi_vo_enable_chn(g_vo_layer, g_vo_chn);
        if (ret != TD_SUCCESS && ret != OT_ERR_VO_NOT_DISABLE) {
            LOG("camera_pipe_vo_set_window: enable_chn ret=%#x", ret);
            return ret;
        }
    }

    g_vo_win_x = x;
    g_vo_win_y = y;
    g_vo_win_w = width;
    g_vo_win_h = height;
    if (g_vo_pip_hide == TD_TRUE) {
        LOG("camera_pipe_vo_set_window: pip-hidden (32x32, VPSS drain on)");
    } else {
        LOG("camera_pipe_vo_set_window: %u,%u %ux%u", (unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height);
    }
    return TD_SUCCESS;
}

td_void camera_pipe_shutdown(td_void)
{
    if (g_preview_on == TD_TRUE) {
        td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM];
        camera_fill_vpss_chn_enable(chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
        sample_comm_vpss_un_bind_vo(g_vpss_grp, g_vpss_chn, g_vo_layer, g_vo_chn);
        sample_comm_vo_stop_vo(&g_vo_cfg);
        sample_common_vpss_stop(g_vpss_grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
        sample_comm_vi_un_bind_vpss(g_vi_pipe, g_vi_chn, g_vpss_grp, g_vpss_chn);
        sample_comm_vi_stop_vi(&g_vi_cfg);
        camera_try_isp_exit_all_pipes();
        g_preview_on = TD_FALSE;
        LOG("preview stopped");
    }

    if (g_mpp_ready == TD_TRUE) {
        sample_comm_sys_exit();
        g_mpp_ready = TD_FALSE;
        LOG("MPP exited");
    }
}
