/*
 * 固件对齐版 sample：用法与 Euler 固件一致
 *   sample_firmware_vio <sensor_index> <venc_en>
 *   sensor_index: 0/1 单路 4lane 与板端 /opt/sample/.../sample_vio 一致
 *   venc_en: 0 仅 VI+VPSS+VO；1 再开 VENC 并绑定
 *
 * VI 拓扑对齐 Gitee：hieulerpi/SS928V100_SDK_V2.0.2.2_MPP_Sample 中 sample_vi_get_one_sensor_vi_cfg()
 *（EULER_2R V1.0：J3 sensor0 -> i2c5 clk0；J4 sensor1 -> VI dev2 + i2c7 clk1，divide_mode=LANE_DIVIDE_MODE_1）。
 * 与主线 smp/.../sample/vio/sample_vio.c（argc==2 用例号）不同，勿混用。
 *
 * 环境变量：WIDGET_CAM_ISP_CLEAN、WIDGET_CAM_SENSOR1_BUS、WIDGET_CAM_SENSOR1_STYLE=dev2、
 * WIDGET_CAM_SENSOR1_SNS_AUX=0、WIDGET_CAM_I2C_BUS、SAMPLE_FW_SOCKET=2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sample_comm.h"
#include "securec.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_isp.h"

#define LOG(fmt, ...) fprintf(stderr, "[sample_fw_vio] " fmt "\n", ##__VA_ARGS__)

/* 与 Euler MPP_Sample sample_vio_one_sensor：VB_YUV_ROUTE_CNT + VB_RAW_CNT_NONE(0) */
#define FW_VB_YUV_CNT 10
#define FW_VB_RAW_CNT 0
#define FW_VB_SUPP    OT_VB_SUPPLEMENT_BNR_MOT_MASK

static sample_vo_cfg g_vo_cfg = {
    .vo_dev            = SAMPLE_VO_DEV_UHD,
    .vo_intf_type      = OT_VO_INTF_HDMI,
    .intf_sync         = OT_VO_OUT_1080P30,
    .bg_color          = COLOR_RGB_BLACK,
    .pix_format        = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
    .disp_rect         = {0, 0, 1920, 1080},
    .image_size        = {1920, 1080},
    .vo_part_mode      = OT_VO_PARTITION_MODE_SINGLE,
    .dis_buf_len       = 3,
    .dst_dynamic_range = OT_DYNAMIC_RANGE_SDR8,
    .vo_mode           = VO_MODE_1MUX,
    .compress_mode     = OT_COMPRESS_MODE_NONE,
};

static sample_comm_venc_chn_param g_venc_chn_param = {
    .frame_rate           = 30,
    .stats_time           = 1,
    .gop                  = 30,
    .venc_size            = {1920, 1080},
    .size                 = PIC_1080P,
    .profile              = 0,
    .is_rcn_ref_share_buf = TD_FALSE,
    .gop_attr             = {
        .gop_mode = OT_VENC_GOP_MODE_NORMAL_P,
        .normal_p = {2},
    },
    .type                 = OT_PT_H265,
    .rc_mode              = SAMPLE_RC_VBR,
};

static td_void fw_try_isp_exit_all_pipes(td_void)
{
    ot_vi_pipe p;
    for (p = 0; p < OT_VI_MAX_PHYS_PIPE_NUM; p++) {
        (td_void)ss_mpi_isp_exit(p);
    }
}

static td_void fw_fill_vb_cfg(ot_size *size, ot_vb_cfg *vb_cfg, ot_vi_video_mode video_mode, td_u32 yuv_cnt,
    td_u32 raw_cnt)
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

static td_s32 fw_sys_init(td_void)
{
    td_s32 ret;
    ot_size size;
    ot_vb_cfg vb_cfg;

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &size);
    fw_fill_vb_cfg(&size, &vb_cfg, OT_VI_VIDEO_MODE_NORM, FW_VB_YUV_CNT, FW_VB_RAW_CNT);

    ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, FW_VB_SUPP);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_sys_init_with_vb_supplement failed, ret=%#x", ret);
        return ret;
    }

    {
        const char *isp_clean = getenv("WIDGET_CAM_ISP_CLEAN");
        if (isp_clean != NULL && isp_clean[0] != '\0' && isp_clean[0] != '0') {
            fw_try_isp_exit_all_pipes();
            LOG("WIDGET_CAM_ISP_CLEAN: ss_mpi_isp_exit all phys pipes");
        }
    }

    ret = sample_comm_vi_set_vi_vpss_mode(OT_VI_ONLINE_VPSS_OFFLINE, OT_VI_VIDEO_MODE_NORM);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_set_vi_vpss_mode failed, ret=%#x", ret);
        sample_comm_sys_exit();
        return ret;
    }
    return TD_SUCCESS;
}

/* 自 camera_pipe.c：双路底板仅接第二插座时用；环境 SAMPLE_FW_SOCKET=2 启用 */
static td_void fw_apply_second_mipi_vi_cfg(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 2;
    const ot_vi_pipe vi_pipe = 1;

    vi_cfg->sns_info.bus_id = 7; /* Euler two_sensor 第二路：i2c7（J4 sensor1） */
    vi_cfg->sns_info.sns_clk_src = 1;
    vi_cfg->sns_info.sns_rst_src = 1;

    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, vi_dev, &vi_cfg->mipi_info);
    vi_cfg->mipi_info.divide_mode = LANE_DIVIDE_MODE_1;
    vi_cfg->dev_info.vi_dev = vi_dev;

    vi_cfg->bind_pipe.pipe_num = 1;
    vi_cfg->bind_pipe.pipe_id[0] = vi_pipe;

    vi_cfg->grp_info.grp_num = 1;
    vi_cfg->grp_info.fusion_grp[0] = 1;
    vi_cfg->grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;

    sample_comm_vi_get_default_pipe_info(SENSOR0_TYPE, &vi_cfg->bind_pipe, vi_cfg->pipe_info);
}

/* Euler MPP_Sample：EULER_2R J3 sensor0 */
static td_void fw_apply_euler_2r_one_sensor0(sample_vi_cfg *vi_cfg)
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

/* Euler MPP_Sample：EULER_2R J4 sensor1（单路，非 two_sensor 的 pipe1） */
static td_void fw_apply_euler_2r_one_sensor1(sample_vi_cfg *vi_cfg)
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

static td_void fw_apply_sensor1_env_overrides(sample_vi_cfg *vi_cfg)
{
    const char *aux;
    const char *b;

    b = getenv("WIDGET_CAM_SENSOR1_BUS");
    if (b != NULL && b[0] != '\0') {
        td_u32 bus = (td_u32)strtoul(b, NULL, 0);
        if (bus <= 15U) {
            vi_cfg->sns_info.bus_id = (td_u8)bus;
            LOG("WIDGET_CAM_SENSOR1_BUS=%u", (unsigned)bus);
        }
    }

    aux = getenv("WIDGET_CAM_SENSOR1_SNS_AUX");
    if (aux != NULL && aux[0] == '0') {
        vi_cfg->sns_info.sns_clk_src = 0;
        vi_cfg->sns_info.sns_rst_src = 0;
        LOG("WIDGET_CAM_SENSOR1_SNS_AUX=0: sns_clk/rst -> 0/0");
    }
}

static td_void fw_apply_dev2_pipe0(sample_vi_cfg *vi_cfg)
{
    const ot_vi_dev vi_dev = 2;
    const ot_vi_pipe vi_pipe = 0;

    vi_cfg->sns_info.bus_id = 7; /* 与 Euler 单路 sensor1 一致；旧版误为 5 */
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

static td_void fw_apply_i2c_bus_env(sample_vi_cfg *vi_cfg)
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
    LOG("WIDGET_CAM_I2C_BUS=%u", (unsigned)bus);
}

static td_void fw_apply_vi_cfg_for_sensor(td_u32 sensor_idx, sample_vi_cfg *vi_cfg)
{
    sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, vi_cfg);

    if (sensor_idx == 1U) {
        const char *st = getenv("WIDGET_CAM_SENSOR1_STYLE");
        const char *sk = getenv("SAMPLE_FW_SOCKET");

        if (sk != NULL && sk[0] == '2') {
            fw_apply_second_mipi_vi_cfg(vi_cfg);
            LOG("sensor1: SAMPLE_FW_SOCKET=2 (VI dev2+pipe1，对齐 Euler two_sensor 第二路 i2c7)");
        } else if (st != NULL && strcmp(st, "dev2") == 0) {
            fw_apply_dev2_pipe0(vi_cfg);
            LOG("sensor1: WIDGET_CAM_SENSOR1_STYLE=dev2（VI dev2+pipe0+i2c7）");
        } else {
            fw_apply_euler_2r_one_sensor1(vi_cfg);
            fw_apply_sensor1_env_overrides(vi_cfg);
            LOG("sensor1: Euler EULER_2R 单路 J4（VI dev2 + i2c7 + clk1）");
        }
    } else {
        fw_apply_euler_2r_one_sensor0(vi_cfg);
        LOG("sensor0: Euler EULER_2R 单路 J3（VI dev0 + i2c5 + clk0）");
    }

    fw_apply_i2c_bus_env(vi_cfg);
}

static td_s32 fw_start_vpss(ot_vpss_grp grp, ot_size *in_size)
{
    td_s32 ret;
    ot_low_delay_info low_delay_info;
    ot_vpss_grp_attr grp_attr;
    ot_vpss_chn_attr chn_attr;
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM] = {TD_TRUE, TD_FALSE, TD_FALSE, TD_FALSE};

    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width  = in_size->width;
    grp_attr.max_height = in_size->height;
    sample_comm_vpss_get_default_chn_attr(&chn_attr);
    chn_attr.width  = in_size->width;
    chn_attr.height = in_size->height;

    ret = sample_common_vpss_start(grp, chn_enable, &grp_attr, &chn_attr, OT_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    low_delay_info.enable     = TD_TRUE;
    low_delay_info.line_cnt   = 200;
    low_delay_info.one_buf_en = TD_FALSE;
    ret = ss_mpi_vpss_set_low_delay_attr(grp, 0, &low_delay_info);
    if (ret != TD_SUCCESS) {
        sample_common_vpss_stop(grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
        return ret;
    }
    return TD_SUCCESS;
}

static td_void fw_stop_vpss(ot_vpss_grp grp)
{
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM] = {TD_TRUE, TD_FALSE, TD_FALSE, TD_FALSE};
    sample_common_vpss_stop(grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
}

static td_s32 fw_start_venc(ot_venc_chn venc_chn[], td_u32 chn_num, const ot_size *in_size)
{
    td_s32 i, ret;

    g_venc_chn_param.venc_size.width  = in_size->width;
    g_venc_chn_param.venc_size.height = in_size->height;
    g_venc_chn_param.size = sample_comm_sys_get_pic_enum(in_size);

    for (i = 0; i < (td_s32)chn_num; i++) {
        ret = sample_comm_venc_start(venc_chn[i], &g_venc_chn_param);
        if (ret != TD_SUCCESS) {
            goto exit;
        }
    }

    ret = sample_comm_venc_start_get_stream(venc_chn, chn_num);
    if (ret != TD_SUCCESS) {
        goto exit;
    }

    return TD_SUCCESS;

exit:
    for (i = i - 1; i >= 0; i--) {
        sample_comm_venc_stop(venc_chn[i]);
    }
    return TD_FAILURE;
}

static td_void fw_stop_venc(ot_venc_chn venc_chn[], td_u32 chn_num)
{
    td_u32 i;

    sample_comm_venc_stop_get_stream(chn_num);

    for (i = 0; i < chn_num; i++) {
        sample_comm_venc_stop(venc_chn[i]);
    }
}

static td_void fw_usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s <sensor_index> <venc_en>\n"
        "  sensor_index: 0/1 对齐 Euler sample_vio（EULER_2R：J3 i2c5 / J4 i2c7+VI dev2）\n"
        "  venc_en: 0=仅 VO 预览, 1=再开 VENC\n"
        "环境变量: WIDGET_CAM_ISP_CLEAN WIDGET_CAM_SENSOR1_BUS WIDGET_CAM_SENSOR1_STYLE=dev2 "
        "WIDGET_CAM_SENSOR1_SNS_AUX=0 WIDGET_CAM_I2C_BUS SAMPLE_FW_SOCKET=2\n",
        argv0);
}

static td_s32 fw_run(td_u32 sensor_idx, td_u32 venc_en)
{
    td_s32 ret;
    sample_vi_cfg vi_cfg;
    ot_size in_size;
    ot_vi_pipe vi_pipe;
    const ot_vi_chn vi_chn = 0;
    ot_vpss_grp vpss_grp;
    const ot_vpss_chn vpss_chn = 0;
    const ot_vo_layer vo_layer = 0;
    ot_vo_chn vo_chn = 0;
    ot_venc_chn venc_chn = 0;
    td_bool vo_started = TD_FALSE;
    td_bool vo_bound = TD_FALSE;
    td_bool vpss_started = TD_FALSE;
    td_bool vi_vpss_bound = TD_FALSE;
    td_bool venc_stream = TD_FALSE;
    td_bool venc_bound = TD_FALSE;

    if (sensor_idx > 1U) {
        LOG("sensor_index 仅支持 0 或 1");
        return TD_FAILURE;
    }

    ret = fw_sys_init();
    if (ret != TD_SUCCESS) {
        return ret;
    }

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_size);
    fw_apply_vi_cfg_for_sensor(sensor_idx, &vi_cfg);

    vi_pipe = vi_cfg.bind_pipe.pipe_id[0];
    vpss_grp = (ot_vpss_grp)vi_pipe;

    ret = sample_comm_vi_start_vi(&vi_cfg);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_start_vi failed, ret=%#x", ret);
        sample_comm_sys_exit();
        return ret;
    }

    ret = sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp, vpss_chn);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vi_bind_vpss failed, ret=%#x", ret);
        goto out_stop_vi;
    }
    vi_vpss_bound = TD_TRUE;

    ret = fw_start_vpss(vpss_grp, &in_size);
    if (ret != TD_SUCCESS) {
        LOG("fw_start_vpss failed, ret=%#x", ret);
        goto out_unbind_vi_vpss;
    }
    vpss_started = TD_TRUE;

    g_vo_cfg.vo_mode = VO_MODE_1MUX;
    ret = sample_comm_vo_start_vo(&g_vo_cfg);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vo_start_vo failed, ret=%#x", ret);
        goto out_stop_vpss;
    }
    vo_started = TD_TRUE;

    ret = sample_comm_vpss_bind_vo(vpss_grp, vpss_chn, vo_layer, vo_chn);
    if (ret != TD_SUCCESS) {
        LOG("sample_comm_vpss_bind_vo failed, ret=%#x", ret);
        goto out_stop_vo;
    }
    vo_bound = TD_TRUE;

    if (venc_en != 0U) {
        ret = fw_start_venc(&venc_chn, 1, &in_size);
        if (ret != TD_SUCCESS) {
            LOG("fw_start_venc failed, ret=%#x", ret);
            goto out_unbind_vo;
        }
        venc_stream = TD_TRUE;
        ret = sample_comm_vpss_bind_venc(vpss_grp, vpss_chn, venc_chn);
        if (ret != TD_SUCCESS) {
            LOG("sample_comm_vpss_bind_venc failed, ret=%#x", ret);
            goto out_stop_venc;
        }
        venc_bound = TD_TRUE;
    }

    LOG("running, Enter 退出");
    sample_pause();
    ret = TD_SUCCESS;

    if (venc_bound == TD_TRUE) {
        sample_comm_vpss_un_bind_venc(vpss_grp, vpss_chn, venc_chn);
    }
out_stop_venc:
    if (venc_stream == TD_TRUE) {
        fw_stop_venc(&venc_chn, 1);
    }
out_unbind_vo:
    if (vo_bound == TD_TRUE) {
        sample_comm_vpss_un_bind_vo(vpss_grp, vpss_chn, vo_layer, vo_chn);
    }
out_stop_vo:
    if (vo_started == TD_TRUE) {
        sample_comm_vo_stop_vo(&g_vo_cfg);
    }
out_stop_vpss:
    if (vpss_started == TD_TRUE) {
        fw_stop_vpss(vpss_grp);
    }
out_unbind_vi_vpss:
    if (vi_vpss_bound == TD_TRUE) {
        sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, vpss_chn);
    }
out_stop_vi:
    sample_comm_vi_stop_vi(&vi_cfg);
    sample_comm_sys_exit();
    return ret;
}

#ifdef __LITEOS__
td_s32 app_main(td_s32 argc, td_char *argv[])
#else
td_s32 main(td_s32 argc, td_char *argv[])
#endif
{
    td_u32 sensor_idx = 1;
    td_u32 venc_en = 0;

    if (argc < 2) {
        fw_usage(argv[0]);
        return TD_FAILURE;
    }

    sensor_idx = (td_u32)strtoul(argv[1], TD_NULL, 0);
    if (argc >= 3) {
        venc_en = (td_u32)strtoul(argv[2], TD_NULL, 0);
    }

    if (fw_run(sensor_idx, venc_en) == TD_SUCCESS) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
        return 0;
    }
    printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    return -1;
}
