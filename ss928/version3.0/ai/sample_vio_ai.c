/*
 * sample_vio_ai.c
 *
 * Goal:
 * - Keep VIO pipeline identical to sample/vio/sample_vio.c (no edits in that file)
 * - Only add: VPSS get frame -> ACL infer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <float.h>
#include <stdint.h>
#include <math.h>

#include "sample_comm.h"
#include "securec.h"
#include "acl.h"
#include "ss_mpi_region.h"
#include "ss_mpi_vo.h"
#include "vio_ai_internal.h"

static const char *g_model_path = "/opt/widget_ui/models/best_aipp_fix.om";

#define YOLO_DEBUG_DIAG 0
#define YOLO_DEBUG_CONF_THRES 0.001f
#define YOLO_DEBUG_ONLY_PERSON 0
#define YOLO_DUMP_INPUT_NV21 1
/* Dump a small crop of the source VPSS YUV before resize/letterbox
 * to check whether stripes are already present in the incoming frame. */
#define YOLO_DUMP_SRC_CROP_640 1

#define ACL_CHK(ret, msg) \
    do { \
        if ((ret) != ACL_SUCCESS) { \
            printf("ACL Error: %s, ret=%d\n", msg, (int)(ret)); \
            return TD_FAILURE; \
        } \
    } while (0)

aclmdlDesc *g_model_desc = TD_NULL;
static aclmdlDataset *g_input_dataset = TD_NULL;
static aclmdlDataset *g_output_dataset = TD_NULL;
uint32_t g_model_id = 0;
static ot_vi_pipe g_active_vi_pipe = 0;
typedef struct {
    float scale;
    float pad_x;
    float pad_y;
    td_u32 net_w;
    td_u32 net_h;
    td_u32 img_w;
    td_u32 img_h;
} yolo_preproc_meta_t;
static yolo_preproc_meta_t g_preproc_meta = {1.0f, 0.0f, 0.0f, 640, 640, 640, 640};

#define YOLO_MAX_DET 256
#define YOLO_NUM_CLASSES 5
#define YOLO_CONF_THRES 0.001f
#define YOLO_IOU_THRES 0.70f
#define YOLO_PRINT_TOPK 5

static yolo_det_t g_draw_dets[YOLO_MAX_DET];
static int g_draw_det_cnt = 0;
static td_bool g_rgn_inited = TD_FALSE;
td_bool g_rgn_chn_ready = TD_FALSE;
static int g_yolo_box_draw = 0;
static td_bool g_attach_pipeline_mode = TD_FALSE;

typedef struct {
    ot_video_frame_info info;
    td_u8 *storage;
    size_t storage_sz;
} vio_ai_owned_frame_t;


/* 拷贝 NV12 后立即 release VPSS 帧，避免 NPU 推理占满 depth 导致 ch0 预览停更 */
static td_s32 vio_ai_own_frame(const ot_video_frame_info *src, vio_ai_owned_frame_t *out)
{
    size_t y_sz;
    size_t uv_sz;
    size_t total;
    td_u8 *buf;
    td_void *y_map = TD_NULL;
    td_void *uv_map = TD_NULL;

    if (src == TD_NULL || out == TD_NULL) {
        return TD_FAILURE;
    }
    (td_void)memset_s(out, sizeof(*out), 0, sizeof(*out));
    y_sz = (size_t)src->video_frame.stride[0] * src->video_frame.height;
    uv_sz = (size_t)src->video_frame.stride[1] * (src->video_frame.height / 2U);
    total = y_sz + uv_sz;
    if (total == 0U) {
        return TD_FAILURE;
    }
    buf = (td_u8 *)malloc(total);
    if (buf == TD_NULL) {
        return TD_FAILURE;
    }

    if (src->video_frame.virt_addr[0] != TD_NULL) {
        (td_void)memcpy_s(buf, total, src->video_frame.virt_addr[0], y_sz);
        if (src->video_frame.virt_addr[1] != TD_NULL) {
            (td_void)memcpy_s(buf + y_sz, total - y_sz, src->video_frame.virt_addr[1], uv_sz);
        }
    } else {
        y_map = ss_mpi_sys_mmap(src->video_frame.phys_addr[0], y_sz);
        uv_map = ss_mpi_sys_mmap(src->video_frame.phys_addr[1], uv_sz);
        if (y_map == TD_NULL || uv_map == TD_NULL) {
            if (y_map != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(y_map, y_sz);
            }
            if (uv_map != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(uv_map, uv_sz);
            }
            free(buf);
            return TD_FAILURE;
        }
        (td_void)memcpy_s(buf, total, y_map, y_sz);
        (td_void)memcpy_s(buf + y_sz, total - y_sz, uv_map, uv_sz);
        (td_void)ss_mpi_sys_munmap(y_map, y_sz);
        (td_void)ss_mpi_sys_munmap(uv_map, uv_sz);
    }

    out->info = *src;
    out->storage = buf;
    out->storage_sz = total;
    out->info.video_frame.virt_addr[0] = buf;
    out->info.video_frame.virt_addr[1] = buf + y_sz;
    out->info.video_frame.phys_addr[0] = 0;
    out->info.video_frame.phys_addr[1] = 0;
    return TD_SUCCESS;
}

static td_void vio_ai_disown_frame(vio_ai_owned_frame_t *owned)
{
    if (owned == TD_NULL) {
        return;
    }
    if (owned->storage != TD_NULL) {
        free(owned->storage);
    }
    (td_void)memset_s(owned, sizeof(*owned), 0, sizeof(*owned));
}

static td_void vio_ai_flush_vpss_chn(ot_vpss_grp grp, ot_vpss_chn chn)
{
    ot_video_frame_info frame;
    td_s32 ret;
    td_u32 n = 0;

    while (n < 16U) {
        (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        ret = ss_mpi_vpss_get_chn_frame(grp, chn, &frame, 0);
        if (ret != TD_SUCCESS) {
            break;
        }
        (td_void)ss_mpi_vpss_release_chn_frame(grp, chn, &frame);
        n++;
    }
    if (n > 0U) {
        printf("attach flush: grp=%d chn=%d released=%u frames\n", grp, chn, n);
    }
}

static td_bool sample_vio_ai_ensure_vpss_depth(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 min_depth)
{
    ot_vpss_chn_attr chn_attr;
    td_s32 ret;

    if (ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr) != TD_SUCCESS) {
        return TD_FALSE;
    }
    if (chn_attr.depth >= min_depth) {
        return TD_FALSE;
    }
    chn_attr.depth = min_depth;
    ret = ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr);
    printf("attach depth bump: grp=%d chn=%d depth=%u ret=0x%x\n",
        grp, chn, min_depth, (td_u32)ret);
    return (ret == TD_SUCCESS) ? TD_TRUE : TD_FALSE;
}

ot_mpp_chn g_rgn_chn = {0};
ot_vpss_grp g_attach_grp = 0;
ot_vpss_chn g_attach_chn = 0;
/* 检测框在 640 网络坐标；叠加到预览通道/VO 时需缩放到实际显示分辨率 */
td_u32 g_rgn_disp_ox = 0;
td_u32 g_rgn_disp_oy = 0;
td_u32 g_rgn_disp_w = 1920;
td_u32 g_rgn_disp_h = 1080;
static td_u32 g_rgn_net_w = 640;
static td_u32 g_rgn_net_h = 640;

#define WIDGET_CAM_VO_STATE "/tmp/.widget_cam_vo"
#define WIDGET_YOLO_ACTION_STATE "/tmp/.widget_yolo_action"
#define WIDGET_YOLO_SWING_EVENTS "/tmp/.widget_yolo_swing_events"
#define YOLO_ACTION_HIST_LEN 10
#define YOLO_ACTION_STABLE_MIN 2
#define YOLO_ACTION_CONF_DEFAULT 0.40f
#define YOLO_SWING_CONF_RATIO 0.85f   /* 挥拍判定可用略低于显示阈值 */
#define YOLO_SWING_COOLDOWN_MS_DEFAULT 1500
#define YOLO_SWING_VEL_THRES_DEFAULT 0.18f
#define YOLO_SWING_PEAK_THRES_DEFAULT 0.28f
#define YOLO_SWING_MIN_ACTIVE_FRAMES 5   /* 需持续运动，避免抬手即触发 */
#define YOLO_SWING_DROP_RATIO 0.55f      /* 峰后需明显回落，短暂停顿不触发 */
#define YOLO_SWING_PEAK_MARGIN 1.25f     /* 峰值相对进入阈值的倍率 */
#define YOLO_SWING_QUIET_FRAMES 8        /* 触发后需安静若干帧再重新武装 */
#define YOLO_SWING_FIRE_SCORE_FLOOR 0.30f  /* 挥拍触发与显示阈值解耦 */
#define YOLO_SERVE_CLASS_ID 4
#define YOLO_SERVE_PHASE_SCALE_DEFAULT 0.05f  /* 挥拍投票里发球全局再降权 */
#define YOLO_SERVE_WIN_RATIO_DEFAULT 3.50f    /* 发球须远超次优非发球才采纳 */
#define YOLO_NON_SERVE_BOOST 1.18f            /* 其它五类微幅加成 */
#define YOLO_RGN_MAX 8
/* 与 SDK sample_comm_region CORNER_RECTEX_MIN_HANDLE 一致（modelzoo 用 corner_rectex） */
#define YOLO_RGN_HANDLE_BASE 140
#define YOLO_SHOW_SCORE_THRES 0.020f
#define YOLO_SHOW_MIN_AREA_RATIO 0.001f
#define YOLO_SHOW_MAX_AREA_RATIO 0.60f
/* YOLOv8 六类羽毛球动作模型：框+类别均由网络输出，勿按人体几何假选 */
#define YOLO_TIER_SMALL_AREA_MAX 0.14f
#define YOLO_TIER_PERSON_AREA_MIN 0.12f
#define YOLO_TIER_PERSON_H_RATIO 0.45f
#define YOLO_TIER_PERSON_W_RATIO 0.22f
#define YOLO_TIER_PERSON_AR_MIN 0.22f
#define YOLO_TIER_PERSON_AR_MAX 0.95f

#define YOLO_TARGET_MODE_SCORE  0  /* 动作模型默认：置信度最高且框合理 */
#define YOLO_TARGET_MODE_PERSON 1
#define YOLO_TARGET_MODE_HAND   2
#define YOLO_ACTION_BOX_MAX_AREA 0.72f
#define YOLO_ACTION_BOX_MIN_AREA 0.008f
#define YOLO_SHOW_MIN_AR 0.30f
#define YOLO_SHOW_MAX_AR 3.50f
#define YOLO_ACTION_TOPK 8
#define YOLO_ACTION_SCORE_THRES 0.0040f
#define YOLO_ACTION_PER_CLASS_TOPK 2
#define YOLO_ACTION_DYNAMIC_THRES_ALPHA 0.30f  /* fraction of current top_p */
#define YOLO_ACTION_DYNAMIC_THRES_MIN 0.0040f
#define YOLO_ACTION_DYNAMIC_THRES_MAX 0.0300f
#define YOLO_TRACK_IOU_THRES 0.06f
#define YOLO_SMOOTH_ALPHA 0.26f
#define YOLO_SMOOTH_SIZE_ALPHA 0.15f
#define YOLO_DISPLAY_SMOOTH_ALPHA 0.30f
#define YOLO_DISPLAY_FPS 40
#define YOLO_HOLD_FRAMES_MAX 12
#define YOLO_TRACK_SLOTS 1
#define YOLO_TRACK_MAX_AGE 12
#define YOLO_TRACK_MAX_CENTER_STEP 0.12f
#define YOLO_TRACK_MAX_SIZE_STEP 0.09f
#define YOLO_VPSS_GET_FRAME_MS 25

typedef struct {
    yolo_det_t box;
    int missed;
    int hits;
} yolo_track_slot_t;

static yolo_track_slot_t g_track_slots[YOLO_TRACK_SLOTS];
static yolo_det_t g_prev_show_dets[YOLO_MAX_DET];
static int g_prev_show_cnt = 0;
td_u32 g_preview_src_w = 1920U;
td_u32 g_preview_src_h = 1080U;
static int g_yolo_show_max = 1;
static float g_yolo_smooth_alpha = YOLO_SMOOTH_ALPHA;
static float g_yolo_smooth_size_alpha = YOLO_SMOOTH_SIZE_ALPHA;
static float g_yolo_track_iou = YOLO_TRACK_IOU_THRES;
static int g_yolo_hold_max = YOLO_HOLD_FRAMES_MAX;
static int g_yolo_target_mode = YOLO_TARGET_MODE_SCORE;
static int g_yolo_draw_nv12 = 0;
static int g_yolo_vpss_get_ms = YOLO_VPSS_GET_FRAME_MS;
static float g_yolo_display_smooth = YOLO_DISPLAY_SMOOTH_ALPHA;
static int g_yolo_disp_interval_ms = 25;
static int g_yolo_use_disp_thread = 1;
static volatile int g_disp_thread_run = 0;
static pthread_t g_disp_thread;
static pthread_mutex_t g_disp_mtx = PTHREAD_MUTEX_INITIALIZER;
static yolo_det_t g_disp_target[YOLO_MAX_DET];
static int g_disp_target_cnt = 0;
static yolo_det_t g_disp_show[YOLO_MAX_DET];
static int g_disp_show_cnt = 0;
static td_u32 g_disp_net_w = 640U;
static td_u32 g_disp_net_h = 640U;
static int g_yolo_tune_inited = 0;
static float g_last_top_p = 0.0f;
static const char *g_cls_names6[YOLO_NUM_CLASSES] = {
    "fangwang", "gaoyuan", "pingchou", "shaqiu", "tiaoqiu"
};
static const char *g_cls_names6_cn[YOLO_NUM_CLASSES] = {
    "放网", "高远", "平抽", "杀球", "挑球"
};
static int g_ai_cls_mode = 0;
#define YOLO_COCO_PERSON_CLASS_ID 0
int g_infer_post_mode = YOLO_INFER_POST_AUTO;

static aclmdlDesc *g_det_model_desc = TD_NULL;
static uint32_t g_det_model_id = 0;
static int g_det_enabled = 0;
static ot_vpss_chn g_det_vpss_chn = 2;
aclmdlDesc *g_pose_model_desc = TD_NULL;
uint32_t g_pose_model_id = 0;
int g_pose_enabled = 0;
int g_pose_rgn_enable = 0;
ot_vpss_chn g_pose_vpss_chn = 2;
td_u32 g_pose_line_thick = 7;
int g_pose_line_auto = 1;
int g_pose_box_draw = 0;
td_bool g_pose_bbox_rgn_inited = TD_FALSE;
td_u32 g_pose_infer_interval = 5;
td_u32 g_pose_hold_ms = 300;
float g_pose_motion_px = 10.0f;
float g_pose_stable_motion_px = 12.0f;
float g_pose_kpt_snap_px = 8.0f;
float g_pose_bbox_jump_px = 28.0f;
float g_pose_smooth_alpha = 0.45f;
td_u32 g_pose_miss_max = 4;
td_u32 g_pose_miss_streak = 0;
int g_pose_clear_on_action = 0;
int g_pose_clear_on_swing = 1;
td_u32 g_pose_det_w = 224U;
td_u32 g_pose_det_h = 224U;
struct timeval g_pose_valid_tv;
td_bool g_pose_valid_ts_set = TD_FALSE;
int g_pose_ch1_only = 1;
int g_replay_live_ring = 0;
td_s32 g_replay_src_chn = -1;
td_bool g_pose_rgn_inited = TD_FALSE;
td_bool g_pose_edge_was_show[POSE_SKELETON_EDGES];
td_bool g_pose_kpt_show[POSE_NUM_KEYPOINTS];
pose_result_t g_pose_result;
static int g_action_cls_hist[YOLO_ACTION_HIST_LEN];
static float g_action_hist_score[YOLO_ACTION_HIST_LEN];
static int g_action_hist_len = 0;
static int g_action_hist_pos = 0;
static int g_yolo_export_action = 1;
static int g_yolo_swing_enable = 1;
static float g_yolo_action_conf_thres = YOLO_ACTION_CONF_DEFAULT;
static int g_swing_cooldown_ms = YOLO_SWING_COOLDOWN_MS_DEFAULT;
static float g_swing_vel_thres = YOLO_SWING_VEL_THRES_DEFAULT;
static float g_swing_peak_thres = YOLO_SWING_PEAK_THRES_DEFAULT;
static float g_swing_fire_score_floor = YOLO_SWING_FIRE_SCORE_FLOOR;
static float g_serve_phase_scale = YOLO_SERVE_PHASE_SCALE_DEFAULT;
static float g_serve_win_ratio = YOLO_SERVE_WIN_RATIO_DEFAULT;

typedef struct {
    td_bool has_prev_motion;
    float prev_cx;
    float prev_cy;
    float prev_cls_score;
    float vel_ema;
    float vel_inst_peak;
    float vel_ema_peak;
    int active_frames;
    int quiet_frames;       /* 低速连续帧，用于触发后重新武装 */
    td_bool armed;          /* 未武装时忽略新的运动窗，防一次挥拍拆成两次 */
    td_u64 last_swing_ms;
    int swing_seq;
    int last_swing_cls;
    float last_swing_score;
    int locked_cls;
    float locked_score;
    int prev_stable;
    td_u32 dbg_frame;
    float dbg_vel_ema;
    float phase_wsum[YOLO_NUM_CLASSES];
    int phase_cnt[YOLO_NUM_CLASSES];
} yolo_swing_state_t;

static yolo_swing_state_t g_swing = {0};

static void yolo_swing_reset(td_void)
{
    (void)memset_s(&g_swing, sizeof(g_swing), 0, sizeof(g_swing));
    g_swing.last_swing_cls = -1;
    g_swing.locked_cls = -1;
    g_swing.armed = TD_TRUE;
}

static float yolo_swing_conf_thres(td_void)
{
    float th = g_yolo_action_conf_thres * YOLO_SWING_CONF_RATIO;
    if (th < 0.20f) {
        th = 0.20f;
    }
    return th;
}

static void yolo_swing_phase_reset(td_void)
{
    (void)memset_s(g_swing.phase_wsum, sizeof(g_swing.phase_wsum), 0, sizeof(g_swing.phase_wsum));
    (void)memset_s(g_swing.phase_cnt, sizeof(g_swing.phase_cnt), 0, sizeof(g_swing.phase_cnt));
}

/* 贴边全高/半屏条：模型常误报为 cls=4 Serve（见日志 xyxy≈(3,6,423,639)） */
static td_bool yolo_action_box_is_false_serve_blob(const yolo_det_t *d, float img_w, float img_h)
{
    float bw;
    float bh;
    float area_ratio;
    float w_ratio;
    float h_ratio;

    if (d == TD_NULL || img_w < 1.0f || img_h < 1.0f) {
        return TD_FALSE;
    }
    bw = d->x2 - d->x1;
    bh = d->y2 - d->y1;
    area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
    w_ratio = bw / img_w;
    h_ratio = bh / img_h;
    if (h_ratio > 0.82f && area_ratio > 0.28f &&
        (d->x1 <= img_w * 0.08f || d->x2 >= img_w * 0.92f)) {
        return TD_TRUE;
    }
    if (w_ratio > 0.45f && h_ratio > 0.82f &&
        (d->y1 <= img_h * 0.05f || d->y2 >= img_h * 0.95f)) {
        return TD_TRUE;
    }
    if (area_ratio > 0.42f && h_ratio > 0.78f) {
        return TD_TRUE;
    }
    if (h_ratio > 0.62f && area_ratio > 0.18f) {
        return TD_TRUE;
    }
    if (area_ratio > 0.22f &&
        (d->x1 <= img_w * 0.10f || d->x2 >= img_w * 0.90f ||
         d->y1 <= img_h * 0.08f || d->y2 >= img_h * 0.92f)) {
        return TD_TRUE;
    }
    return TD_FALSE;
}

/* 仅中小框、居中、非贴边时才可能是真发球 */
static td_bool yolo_action_box_is_plausible_serve(const yolo_det_t *d, float img_w, float img_h)
{
    float bw;
    float bh;
    float area_ratio;
    float w_ratio;
    float h_ratio;
    float cx_ratio;

    if (d == TD_NULL || img_w < 1.0f || img_h < 1.0f) {
        return TD_FALSE;
    }
    if (yolo_action_box_is_false_serve_blob(d, img_w, img_h) == TD_TRUE) {
        return TD_FALSE;
    }
    bw = d->x2 - d->x1;
    bh = d->y2 - d->y1;
    area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
    w_ratio = bw / img_w;
    h_ratio = bh / img_h;
    cx_ratio = ((d->x1 + d->x2) * 0.5f) / img_w;
    if (area_ratio < 0.05f || area_ratio > 0.32f) {
        return TD_FALSE;
    }
    if (h_ratio > 0.68f || w_ratio > 0.58f) {
        return TD_FALSE;
    }
    if (cx_ratio < 0.20f || cx_ratio > 0.80f) {
        return TD_FALSE;
    }
    return TD_TRUE;
}

static td_bool yolo_action_box_valid(const yolo_det_t *d, float img_w, float img_h);
static float yolo_action_box_quality_mul(const yolo_det_t *d, float img_w, float img_h);

static float yolo_action_swing_cls_weight(int cls_id, const yolo_det_t *d, float img_w, float img_h)
{
    if (cls_id != YOLO_SERVE_CLASS_ID || d == TD_NULL) {
        return 1.0f;
    }
    if (yolo_action_box_is_false_serve_blob(d, img_w, img_h) == TD_TRUE) {
        return 0.0f;
    }
    if (yolo_action_box_is_plausible_serve(d, img_w, img_h) == TD_TRUE) {
        return 0.35f;
    }
    return 0.04f;
}

static void yolo_swing_phase_accum(int cls, float score)
{
    float th = yolo_swing_conf_thres();
    /* 非发球类略放宽，避免挥拍窗内只有发球票 */
    if (cls != YOLO_SERVE_CLASS_ID) {
        th *= 0.55f;
    }

    if (cls < 0 || cls >= YOLO_NUM_CLASSES || score < th) {
        return;
    }
    if (cls == YOLO_SERVE_CLASS_ID) {
        score *= g_serve_phase_scale;
    } else {
        score *= YOLO_NON_SERVE_BOOST;
    }
    g_swing.phase_wsum[cls] += score;
    g_swing.phase_cnt[cls]++;
}

/* 挥拍窗口：NMS 各类别最佳框各计一票，避免 top1 贴边 Serve 垄断 */
static void yolo_swing_phase_accum_nms(const yolo_det_t *src, int src_cnt, float img_w, float img_h)
{
    float best[YOLO_NUM_CLASSES];
    int c;
    int i;

    (void)memset_s(best, sizeof(best), 0, sizeof(best));
    if (src == TD_NULL || src_cnt <= 0) {
        return;
    }
    if (img_w < 1.0f) {
        img_w = 640.0f;
    }
    if (img_h < 1.0f) {
        img_h = 640.0f;
    }
    for (i = 0; i < src_cnt; i++) {
        int cls = src[i].cls_id;
        float wscore;

        if (cls < 0 || cls >= YOLO_NUM_CLASSES) {
            continue;
        }
        {
            float bw = src[i].x2 - src[i].x1;
            float bh = src[i].y2 - src[i].y1;
            float area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
            if (bw < 8.0f || bh < 8.0f || area_ratio < 0.002f || area_ratio > 0.88f) {
                continue;
            }
        }
        wscore = src[i].score * yolo_action_box_quality_mul(&src[i], img_w, img_h) *
            yolo_action_swing_cls_weight(cls, &src[i], img_w, img_h);
        if (wscore > best[cls]) {
            best[cls] = wscore;
        }
    }
    for (c = 0; c < YOLO_NUM_CLASSES; c++) {
        if (best[c] > 0.0f) {
            yolo_swing_phase_accum(c, best[c]);
        }
    }
}

static int yolo_swing_phase_pick_best_non_serve(float *out_score)
{
    int c;
    int best_c = -1;
    float best_w = 0.0f;

    for (c = 0; c < YOLO_NUM_CLASSES; c++) {
        if (c == YOLO_SERVE_CLASS_ID) {
            continue;
        }
        if (g_swing.phase_wsum[c] > best_w) {
            best_w = g_swing.phase_wsum[c];
            best_c = c;
        }
    }
    if (best_c >= 0 && out_score != TD_NULL) {
        if (g_swing.phase_cnt[best_c] > 0) {
            *out_score = best_w / (float)g_swing.phase_cnt[best_c];
        } else {
            *out_score = best_w;
        }
    }
    return best_c;
}

static int yolo_swing_phase_pick_cls(float *out_score)
{
    int best_c = -1;
    int non_serve_c = -1;
    float best_w = 0.0f;
    float non_serve_w = 0.0f;
    float non_sc = 0.0f;
    float serve_w = g_swing.phase_wsum[YOLO_SERVE_CLASS_ID];

    non_serve_c = yolo_swing_phase_pick_best_non_serve(&non_sc);
    non_serve_w = (non_serve_c >= 0) ? g_swing.phase_wsum[non_serve_c] : 0.0f;

    if (non_serve_c >= 0 && non_serve_w > 0.0f) {
        if (serve_w < non_serve_w * g_serve_win_ratio) {
            best_c = non_serve_c;
            best_w = non_serve_w;
        } else {
            best_c = YOLO_SERVE_CLASS_ID;
            best_w = serve_w;
        }
    } else if (serve_w > 0.0f) {
        best_c = YOLO_SERVE_CLASS_ID;
        best_w = serve_w;
    }

    if (best_c >= 0 && out_score != TD_NULL) {
        if (g_swing.phase_cnt[best_c] > 0) {
            *out_score = best_w / (float)g_swing.phase_cnt[best_c];
        } else {
            *out_score = best_w;
        }
    }
    return best_c;
}

static td_void yolo_swing_fire(int cls, float sc, td_u64 ts_ms, const char *reason, float vel_peak)
{
    int fire_cls = cls;
    float fire_sc = sc;

    if ((ts_ms - g_swing.last_swing_ms) < (td_u64)g_swing_cooldown_ms) {
        return;
    }
    if (fire_cls < 0 || fire_cls >= YOLO_NUM_CLASSES) {
        fire_cls = yolo_swing_phase_pick_best_non_serve(&fire_sc);
    }
    if (fire_cls < 0) {
        return;
    }
    if (fire_cls == YOLO_SERVE_CLASS_ID) {
        int alt = yolo_swing_phase_pick_best_non_serve(&fire_sc);
        if (alt >= 0) {
            fire_cls = alt;
        } else {
            return;
        }
    }
    if (fire_sc < g_swing_fire_score_floor) {
        return;
    }
    cls = fire_cls;
    sc = fire_sc;
    if (cls < 0 || cls >= YOLO_NUM_CLASSES) {
        return;
    }
    g_swing.swing_seq++;
    g_swing.last_swing_cls = cls;
    g_swing.last_swing_score = sc;
    g_swing.last_swing_ms = ts_ms;
    if (g_pose_enabled != 0 && g_pose_clear_on_swing != 0 && g_pose_rgn_enable != 0) {
        pose_rgn_clear_now();
    }
    printf("yolo cam-swing #%d cls=%d(%s) score=%.3f peak_vel=%.3f reason=%s\n",
        g_swing.swing_seq, cls, g_cls_names6_cn[cls], sc, vel_peak, reason);
    {
        FILE *ef = fopen(WIDGET_YOLO_SWING_EVENTS, "a");
        if (ef != TD_NULL) {
            (void)fprintf(ef, "%d %d %.4f %llu\n",
                g_swing.swing_seq, cls, sc, (unsigned long long)ts_ms);
            (void)fclose(ef);
        }
    }
}

static void yolo_swing_lock_cls(const yolo_det_t *cls_src, int cls_cnt, int stable_cls, float score, int stable)
{
    if (stable != 0 && stable_cls >= 0 && stable_cls < YOLO_NUM_CLASSES &&
        stable_cls != YOLO_SERVE_CLASS_ID && score >= g_yolo_action_conf_thres) {
        g_swing.locked_cls = stable_cls;
        g_swing.locked_score = score;
    } else if (cls_cnt > 0 && cls_src != TD_NULL && cls_src[0].cls_id >= 0 &&
        cls_src[0].cls_id < YOLO_NUM_CLASSES && cls_src[0].cls_id != YOLO_SERVE_CLASS_ID &&
        cls_src[0].score >= g_yolo_action_conf_thres) {
        g_swing.locked_cls = cls_src[0].cls_id;
        g_swing.locked_score = cls_src[0].score;
    }
}

/*
 * motion_src：NMS 后原始框（未跟踪平滑），用于挥拍速度
 * cls_src：跟踪平滑后框，用于动作类别
 */
static void yolo_swing_on_frame(const yolo_det_t *motion_src, int motion_cnt,
    const yolo_det_t *cls_src, int cls_cnt, const yolo_det_t *nms_src, int nms_cnt,
    float img_w, float img_h, int stable_cls, float score, int stable, td_u64 ts_ms, td_u32 frame_idx)
{
    float vel = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float diag = 8.0f;
    if (g_yolo_swing_enable == 0) {
        return;
    }

    if (motion_cnt > 0 && motion_src != TD_NULL) {
        float bw = motion_src[0].x2 - motion_src[0].x1;
        float bh = motion_src[0].y2 - motion_src[0].y1;
        cx = (motion_src[0].x1 + motion_src[0].x2) * 0.5f;
        cy = (motion_src[0].y1 + motion_src[0].y2) * 0.5f;
        diag = sqrtf(bw * bw + bh * bh);
        if (diag < 8.0f) {
            diag = 8.0f;
        }
        if (g_swing.has_prev_motion == TD_TRUE) {
            float dx = cx - g_swing.prev_cx;
            float dy = cy - g_swing.prev_cy;
            vel = sqrtf(dx * dx + dy * dy) / diag;
        }
        g_swing.prev_cx = cx;
        g_swing.prev_cy = cy;
        g_swing.has_prev_motion = TD_TRUE;
    } else if (g_ai_cls_mode != 0 && cls_cnt > 0 && cls_src != TD_NULL) {
        /* 分类模式无检测框：用置信度跳变近似挥拍速度 */
        float sc = (score > 0.01f) ? score : cls_src[0].score;
        if (g_swing.has_prev_motion == TD_TRUE) {
            vel = fabsf(sc - g_swing.prev_cls_score) * 3.5f;
        }
        g_swing.prev_cls_score = sc;
        g_swing.has_prev_motion = TD_TRUE;
    } else {
        vel = 0.0f;
    }

    g_swing.vel_ema = g_swing.vel_ema * 0.45f + vel * 0.55f;
    if (vel > g_swing.vel_inst_peak) {
        g_swing.vel_inst_peak = vel;
    }
    g_swing.dbg_vel_ema = g_swing.vel_ema;
    g_swing.dbg_frame = frame_idx;

    yolo_swing_lock_cls(cls_src, cls_cnt, stable_cls, score, stable);
    g_swing.prev_stable = stable;

    /* 触发后需连续低速若干帧才重新武装，避免一次挥拍被拆成抬手+出拍两次 */
    if (g_swing.armed != TD_TRUE) {
        if (g_swing.vel_ema <= g_swing_vel_thres * 0.40f && vel <= g_swing_vel_thres * 0.40f) {
            g_swing.quiet_frames++;
            if (g_swing.quiet_frames >= YOLO_SWING_QUIET_FRAMES) {
                g_swing.armed = TD_TRUE;
                g_swing.quiet_frames = 0;
                g_swing.active_frames = 0;
                g_swing.vel_inst_peak = 0.0f;
                g_swing.vel_ema_peak = 0.0f;
                yolo_swing_phase_reset();
            }
        } else {
            g_swing.quiet_frames = 0;
        }
        if ((frame_idx % 90) == 0 && motion_cnt > 0) {
            printf("yolo swing dbg frame=%u vel=%.3f ema=%.3f peak=%.3f active=%d armed=0 quiet=%d\n",
                frame_idx, vel, g_swing.vel_ema, g_swing.vel_inst_peak, g_swing.active_frames,
                g_swing.quiet_frames);
        }
        return;
    }

    if (g_swing.vel_ema > g_swing_vel_thres || vel > g_swing_vel_thres) {
        if (g_swing.active_frames == 0) {
            yolo_swing_phase_reset();
        }
        g_swing.active_frames++;
        if (g_swing.vel_ema > g_swing.vel_ema_peak) {
            g_swing.vel_ema_peak = g_swing.vel_ema;
        }
        if (nms_cnt > 0 && nms_src != TD_NULL) {
            yolo_swing_phase_accum_nms(nms_src, nms_cnt, img_w, img_h);
        } else if (cls_cnt > 0 && cls_src != TD_NULL) {
            float w = cls_src[0].score * yolo_action_swing_cls_weight(cls_src[0].cls_id, &cls_src[0], img_w, img_h);
            yolo_swing_phase_accum(cls_src[0].cls_id, w);
        }
    }

    /* 挥拍：持续运动 + 足够峰值 + 明显回落；抬手短暂停顿不触发 */
    {
        float need_peak = g_swing_peak_thres;
        float margin_peak = g_swing_vel_thres * YOLO_SWING_PEAK_MARGIN;
        float vp = (g_swing.vel_inst_peak > g_swing.vel_ema_peak) ?
            g_swing.vel_inst_peak : g_swing.vel_ema_peak;
        if (margin_peak > need_peak) {
            need_peak = margin_peak;
        }

        if (g_swing.active_frames >= YOLO_SWING_MIN_ACTIVE_FRAMES &&
            vp >= need_peak &&
            g_swing.vel_ema < g_swing_vel_thres * YOLO_SWING_DROP_RATIO) {
            int cls_fire = -1;
            float sc_fire = 0.0f;

            cls_fire = yolo_swing_phase_pick_cls(&sc_fire);
            if (cls_fire < 0 && g_swing.locked_cls >= 0 && g_swing.locked_cls != YOLO_SERVE_CLASS_ID) {
                cls_fire = g_swing.locked_cls;
                sc_fire = g_swing.locked_score;
            }
            if (cls_fire < 0 && stable_cls >= 0 && stable_cls != YOLO_SERVE_CLASS_ID) {
                cls_fire = stable_cls;
                sc_fire = score;
            }
            if (cls_fire == YOLO_SERVE_CLASS_ID) {
                int alt = yolo_swing_phase_pick_best_non_serve(&sc_fire);
                if (alt >= 0) {
                    cls_fire = alt;
                }
            }
            if (cls_fire < 0 && motion_cnt > 0 && motion_src != TD_NULL &&
                motion_src[0].cls_id >= 0 && motion_src[0].cls_id < YOLO_NUM_CLASSES &&
                motion_src[0].cls_id != YOLO_SERVE_CLASS_ID) {
                cls_fire = motion_src[0].cls_id;
                sc_fire = motion_src[0].score;
            }
            yolo_swing_fire(cls_fire, sc_fire, ts_ms, "motion_peak", vp);
            g_swing.active_frames = 0;
            g_swing.vel_inst_peak = 0.0f;
            g_swing.vel_ema_peak = 0.0f;
            g_swing.locked_cls = -1;
            g_swing.locked_score = 0.0f;
            g_swing.armed = TD_FALSE;
            g_swing.quiet_frames = 0;
            yolo_swing_phase_reset();
        } else if (g_swing.vel_ema <= g_swing_vel_thres * 0.35f) {
            if (g_swing.active_frames > 0) {
                g_swing.active_frames--;
            }
            if (g_swing.active_frames == 0) {
                yolo_swing_phase_reset();
            }
            g_swing.vel_inst_peak *= 0.70f;
            g_swing.vel_ema_peak *= 0.70f;
        }
    }

    if ((frame_idx % 90) == 0 && motion_cnt > 0) {
        printf("yolo swing dbg frame=%u vel=%.3f ema=%.3f peak=%.3f active=%d cls=%d stable=%d armed=1\n",
            frame_idx, vel, g_swing.vel_ema, g_swing.vel_inst_peak, g_swing.active_frames,
            (cls_cnt > 0 && cls_src != TD_NULL) ? cls_src[0].cls_id : -1, stable);
    }
}

static void yolo_action_hist_push(int cls, float score)
{
    if (cls < 0 || cls >= YOLO_NUM_CLASSES) {
        return;
    }
    g_action_cls_hist[g_action_hist_pos] = cls;
    g_action_hist_score[g_action_hist_pos] = score;
    g_action_hist_pos = (g_action_hist_pos + 1) % YOLO_ACTION_HIST_LEN;
    if (g_action_hist_len < YOLO_ACTION_HIST_LEN) {
        g_action_hist_len++;
    }
}

static int yolo_action_hist_majority(int *out_cls, int *out_votes)
{
    float wsum[YOLO_NUM_CLASSES];
    int counts[YOLO_NUM_CLASSES];
    int i;
    int c;
    int best_c = -1;
    int best_n = 0;
    float best_w = 0.0f;

    (void)memset_s(wsum, sizeof(wsum), 0, sizeof(wsum));
    (void)memset_s(counts, sizeof(counts), 0, sizeof(counts));
    for (i = 0; i < g_action_hist_len; i++) {
        int id = g_action_cls_hist[i];
        float w = g_action_hist_score[i];
        if (id >= 0 && id < YOLO_NUM_CLASSES) {
            counts[id]++;
            wsum[id] += w;
        }
    }
    for (c = 0; c < YOLO_NUM_CLASSES; c++) {
        if (wsum[c] > best_w || (fabsf(wsum[c] - best_w) < 1e-6f && counts[c] > best_n)) {
            best_w = wsum[c];
            best_n = counts[c];
            best_c = c;
        }
    }
    if (out_cls != TD_NULL) {
        *out_cls = best_c;
    }
    if (out_votes != TD_NULL) {
        *out_votes = best_n;
    }
    return best_c;
}

static float yolo_action_hist_majority_score(int cls)
{
    float wsum[YOLO_NUM_CLASSES];
    int counts[YOLO_NUM_CLASSES];
    int i;

    (void)memset_s(wsum, sizeof(wsum), 0, sizeof(wsum));
    (void)memset_s(counts, sizeof(counts), 0, sizeof(counts));
    for (i = 0; i < g_action_hist_len; i++) {
        int id = g_action_cls_hist[i];
        if (id >= 0 && id < YOLO_NUM_CLASSES) {
            wsum[id] += g_action_hist_score[i];
            counts[id]++;
        }
    }
    if (cls < 0 || cls >= YOLO_NUM_CLASSES || counts[cls] <= 0) {
        return 0.0f;
    }
    return wsum[cls] / (float)counts[cls];
}

static void yolo_export_action_state(const yolo_det_t *motion_src, int motion_cnt,
    const yolo_det_t *dets, int cnt, const yolo_det_t *nms_src, int nms_cnt,
    td_u32 img_w, td_u32 img_h, td_u32 frame_idx)
{
    FILE *fp;
    struct timeval tv;
    td_u64 ts_ms = 0;
    int cls = -1;
    float score = 0.0f;
    int stable = 0;
    int maj_cls = -1;
    int maj_votes = 0;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    if (g_yolo_export_action == 0) {
        return;
    }

    if (cnt > 0 && dets != TD_NULL) {
        cls = dets[0].cls_id;
        score = dets[0].score;
        x1 = dets[0].x1;
        y1 = dets[0].y1;
        x2 = dets[0].x2;
        y2 = dets[0].y2;
        if (cls != YOLO_SERVE_CLASS_ID ||
            yolo_action_box_is_plausible_serve(&dets[0], (float)img_w, (float)img_h) == TD_TRUE) {
            if (yolo_action_box_is_false_serve_blob(&dets[0], (float)img_w, (float)img_h) == TD_FALSE) {
                yolo_action_hist_push(cls, score);
            }
        }
    } else if (motion_cnt > 0 && motion_src != TD_NULL) {
        cls = motion_src[0].cls_id;
        score = motion_src[0].score;
        x1 = motion_src[0].x1;
        y1 = motion_src[0].y1;
        x2 = motion_src[0].x2;
        y2 = motion_src[0].y2;
    }

    (void)yolo_action_hist_majority(&maj_cls, &maj_votes);
    if (maj_cls >= 0 && maj_votes >= YOLO_ACTION_STABLE_MIN) {
        float maj_score = yolo_action_hist_majority_score(maj_cls);
        if (maj_score >= g_yolo_action_conf_thres) {
            static int s_pose_action_cls = -1;

            stable = 1;
            cls = maj_cls;
            score = maj_score;
            if (g_pose_enabled != 0 && g_pose_clear_on_action != 0 && g_pose_rgn_enable != 0 && cls != s_pose_action_cls) {
                if (s_pose_action_cls >= 0) {
                    pose_rgn_clear_now();
                }
                s_pose_action_cls = cls;
            } else if (g_pose_enabled != 0 && cls != s_pose_action_cls) {
                s_pose_action_cls = cls;
            }
        }
    }

    if (gettimeofday(&tv, TD_NULL) == 0) {
        ts_ms = (td_u64)tv.tv_sec * 1000ULL + (td_u64)(tv.tv_usec / 1000);
    }

    if (cls < 0 || cls >= YOLO_NUM_CLASSES) {
        cls = -1;
    }

    yolo_swing_on_frame(motion_src, motion_cnt, dets, cnt, nms_src, nms_cnt,
        (float)img_w, (float)img_h, cls, score, stable, ts_ms, frame_idx);

    fp = fopen(WIDGET_YOLO_ACTION_STATE, "w");
    if (fp == TD_NULL) {
        return;
    }
    /* ts frame score stable cls x1 y1 x2 y2 swing_seq swing_cls swing_score vel_ema */
    fprintf(fp, "%llu %u %.4f %d %d %.1f %.1f %.1f %.1f %d %d %.4f %.3f\n",
        (unsigned long long)ts_ms, (unsigned)frame_idx, score, stable, cls, x1, y1, x2, y2,
        g_swing.swing_seq, g_swing.last_swing_cls, g_swing.last_swing_score, g_swing.dbg_vel_ema);
    (void)fclose(fp);
}

static int ai_detect_cls_mode(td_void)
{
    const char *mode = getenv("WIDGET_AI_MODE");
    aclmdlIODims odims;
    size_t out_elems = 1;
    td_u32 i;

    if ((mode != TD_NULL) && (strcmp(mode, "cls") == 0)) {
        return 1;
    }
    if (g_model_desc == TD_NULL) {
        return 0;
    }
    if (aclmdlGetOutputDims(g_model_desc, 0, &odims) != ACL_SUCCESS) {
        return 0;
    }
    for (i = 0; i < odims.dimCount; i++) {
        out_elems *= (size_t)odims.dims[i];
    }
    return (out_elems == (size_t)YOLO_NUM_CLASSES) ? 1 : 0;
}

static void cls_postprocess_and_export(const float *logits, size_t n, td_u32 img_w, td_u32 img_h)
{
    static td_u32 frame_idx = 0;
    yolo_det_t det;
    float max_logit;
    float sum = 0.0f;
    float probs[YOLO_NUM_CLASSES];
    float score = 0.0f;
    int cls = 0;
    int i;
    int stable = 0;
    int maj_cls = -1;
    int maj_votes = 0;

    frame_idx++;
    if ((logits == TD_NULL) || (n < (size_t)YOLO_NUM_CLASSES)) {
        return;
    }

    max_logit = logits[0];
    for (i = 1; i < YOLO_NUM_CLASSES; i++) {
        if (logits[i] > max_logit) {
            max_logit = logits[i];
        }
    }
    for (i = 0; i < YOLO_NUM_CLASSES; i++) {
        probs[i] = expf(logits[i] - max_logit);
        sum += probs[i];
    }
    if (sum <= 0.0f) {
        return;
    }
    for (i = 0; i < YOLO_NUM_CLASSES; i++) {
        probs[i] /= sum;
        if (probs[i] > score) {
            score = probs[i];
            cls = i;
        }
    }

    if (score >= g_yolo_action_conf_thres) {
        yolo_action_hist_push(cls, score);
    } else if (score >= g_yolo_action_conf_thres * 0.75f) {
        /* 略低于显示阈值也入历史，便于稳定识别 */
        yolo_action_hist_push(cls, score);
    }

    (void)yolo_action_hist_majority(&maj_cls, &maj_votes);
    if ((maj_cls >= 0) && (maj_votes >= YOLO_ACTION_STABLE_MIN)) {
        float maj_score = yolo_action_hist_majority_score(maj_cls);
        if (maj_score >= g_yolo_action_conf_thres) {
            stable = 1;
            cls = maj_cls;
            score = maj_score;
        } else if (maj_score >= g_yolo_action_conf_thres * 0.80f) {
            stable = 1;
            cls = maj_cls;
            score = maj_score;
        }
    }

    (void)memset_s(&det, sizeof(det), 0, sizeof(det));
    det.cls_id = cls;
    det.score = score;
    det.x1 = 0.0f;
    det.y1 = 0.0f;
    det.x2 = (float)img_w;
    det.y2 = (float)img_h;

    g_draw_det_cnt = 0;
    yolo_export_action_state(TD_NULL, 0, &det, 1, TD_NULL, 0, img_w, img_h, frame_idx);

    if ((frame_idx % 30U) == 1U) {
        printf("cls: frame=%u cls=%d(%s) score=%.3f stable=%d\n",
            frame_idx, cls, g_cls_names6[cls], score, stable);
    }
}

static int g_ab_inited = 0;
static int g_ab_letterbox = 0;
static int g_ab_feed_nv12 = 0; /* if 1, feed NV12 instead of NV21 into model */
static int g_ab_bilinear = 0;  /* if 1, use bilinear resize instead of NN */
static int g_ab_y_add = 0;     /* add to Y plane after resize */
static int g_ab_uv_add = 0;    /* add to UV plane after resize */
static int g_ab_y_mul_q8 = 256;   /* Y multiplier in Q8 (256=1.0) */
static int g_ab_uv_mul_q8 = 256;  /* UV multiplier in Q8 (256=1.0) */
static float g_decode_conf_thres = YOLO_CONF_THRES;
static float g_decode_iou_thres = YOLO_IOU_THRES;


static void yolo_ab_init_once(void)
{
    if (g_ab_inited) return;
    g_ab_inited = 1;
    /* Strict alignment mode: lock a single deterministic pipeline. */
    g_ab_letterbox = 1;
    g_ab_feed_nv12 = 1;
    g_ab_bilinear = 1;
    g_ab_y_add = 0;
    g_ab_uv_add = 0;
    g_ab_y_mul_q8 = 256;
    g_ab_uv_mul_q8 = 256;
    g_decode_conf_thres = YOLO_CONF_THRES;
    g_decode_iou_thres = YOLO_IOU_THRES;
    if (g_decode_conf_thres < 0.0f) g_decode_conf_thres = 0.0f;
    if (g_decode_conf_thres > 1.0f) g_decode_conf_thres = 1.0f;
    if (g_decode_iou_thres < 0.01f) g_decode_iou_thres = 0.01f;
    if (g_decode_iou_thres > 0.99f) g_decode_iou_thres = 0.99f;
    if (g_ab_y_mul_q8 <= 0) g_ab_y_mul_q8 = 256;
    if (g_ab_uv_mul_q8 <= 0) g_ab_uv_mul_q8 = 256;
    printf("yolo ab: letterbox=%d feed_nv12=%d bilinear=%d y_mul_q8=%d y_add=%d uv_mul_q8=%d uv_add=%d conf=%.3f iou=%.2f\n",
        g_ab_letterbox, g_ab_feed_nv12, g_ab_bilinear, g_ab_y_mul_q8, g_ab_y_add, g_ab_uv_mul_q8, g_ab_uv_add,
        g_decode_conf_thres, g_decode_iou_thres);
}

static void yolo_tune_init_once(void)
{
    if (g_yolo_tune_inited != 0) {
        return;
    }
    g_yolo_tune_inited = 1;
    {
        FILE *ef = fopen(WIDGET_YOLO_SWING_EVENTS, "w");
        if (ef != TD_NULL) {
            (void)fclose(ef);
        }
    }
    g_yolo_box_draw = vio_ai_env_get_int_default("WIDGET_YOLO_BOX_DRAW", 0);
    g_yolo_show_max = vio_ai_env_get_int_default("WIDGET_YOLO_SHOW_MAX", g_yolo_box_draw ? 1 : 0);
    if (g_yolo_box_draw == 0) {
        g_yolo_show_max = 0;
    } else {
        if (g_yolo_show_max < 1) {
            g_yolo_show_max = 1;
        }
        if (g_yolo_show_max > YOLO_RGN_MAX) {
            g_yolo_show_max = YOLO_RGN_MAX;
        }
    }
    g_yolo_smooth_alpha = vio_ai_env_get_float_default("WIDGET_YOLO_SMOOTH", YOLO_SMOOTH_ALPHA);
    if (g_yolo_smooth_alpha < 0.08f) {
        g_yolo_smooth_alpha = 0.08f;
    }
    if (g_yolo_smooth_alpha > 0.90f) {
        g_yolo_smooth_alpha = 0.90f;
    }
    g_yolo_smooth_size_alpha = vio_ai_env_get_float_default("WIDGET_YOLO_SMOOTH_SIZE", YOLO_SMOOTH_SIZE_ALPHA);
    if (g_yolo_smooth_size_alpha < 0.05f) {
        g_yolo_smooth_size_alpha = 0.05f;
    }
    if (g_yolo_smooth_size_alpha > g_yolo_smooth_alpha) {
        g_yolo_smooth_size_alpha = g_yolo_smooth_alpha;
    }
    g_yolo_track_iou = vio_ai_env_get_float_default("WIDGET_YOLO_TRACK_IOU", YOLO_TRACK_IOU_THRES);
    if (g_yolo_track_iou < 0.05f) {
        g_yolo_track_iou = 0.05f;
    }
    if (g_yolo_track_iou > 0.80f) {
        g_yolo_track_iou = 0.80f;
    }
    g_yolo_hold_max = vio_ai_env_get_int_default("WIDGET_YOLO_HOLD", YOLO_HOLD_FRAMES_MAX);
    if (g_yolo_hold_max < 1) {
        g_yolo_hold_max = 1;
    }
    if (g_yolo_hold_max > 15) {
        g_yolo_hold_max = 15;
    }
    {
        int disp_fps = vio_ai_env_get_int_default("WIDGET_YOLO_DISP_FPS", YOLO_DISPLAY_FPS);
        if (disp_fps < 10) {
            disp_fps = 10;
        }
        if (disp_fps > 60) {
            disp_fps = 60;
        }
        g_yolo_disp_interval_ms = 1000 / disp_fps;
        if (g_yolo_disp_interval_ms < 10) {
            g_yolo_disp_interval_ms = 10;
        }
    }
    g_yolo_vpss_get_ms = vio_ai_env_get_int_default("WIDGET_YOLO_FRAME_MS", YOLO_VPSS_GET_FRAME_MS);
    if (g_yolo_vpss_get_ms < 10) {
        g_yolo_vpss_get_ms = 10;
    }
    if (g_yolo_vpss_get_ms > 200) {
        g_yolo_vpss_get_ms = 200;
    }
    g_yolo_display_smooth = vio_ai_env_get_float_default("WIDGET_YOLO_DISPLAY_SMOOTH", YOLO_DISPLAY_SMOOTH_ALPHA);
    if (g_yolo_display_smooth < 0.08f) {
        g_yolo_display_smooth = 0.08f;
    }
    if (g_yolo_display_smooth > 0.90f) {
        g_yolo_display_smooth = 0.90f;
    }
    g_yolo_use_disp_thread = vio_ai_env_get_int_default("WIDGET_YOLO_DISP_THREAD", 1);
    if (g_yolo_box_draw == 0 && g_yolo_show_max == 0) {
        g_yolo_use_disp_thread = 0;
    }
    g_pose_line_thick = (td_u32)vio_ai_env_get_int_default("WIDGET_POSE_LINE_THICK", 7);
    if (g_pose_line_thick < 2U) {
        g_pose_line_thick = 2U;
    }
    if (g_pose_line_thick > 8U) {
        g_pose_line_thick = 8U;
    }
    g_pose_line_auto = vio_ai_env_get_int_default("WIDGET_POSE_LINE_AUTO", 1);
    g_pose_box_draw = vio_ai_env_get_int_default("WIDGET_POSE_BOX_DRAW", 0);
    g_pose_infer_interval = (td_u32)vio_ai_env_get_int_default("WIDGET_POSE_INTERVAL", 2);
    if (g_pose_infer_interval < 2U) {
        g_pose_infer_interval = 2U;
    }
    g_pose_hold_ms = (td_u32)vio_ai_env_get_int_default("WIDGET_POSE_HOLD_MS", 120);
    if (g_pose_hold_ms < 40U) {
        g_pose_hold_ms = 40U;
    }
    if (g_pose_hold_ms > 800U) {
        g_pose_hold_ms = 800U;
    }
    g_pose_motion_px = vio_ai_env_get_float_default("WIDGET_POSE_MOTION_PX", 16.0f);
    if (g_pose_motion_px < 4.0f) {
        g_pose_motion_px = 4.0f;
    }
    if (g_pose_motion_px > 80.0f) {
        g_pose_motion_px = 80.0f;
    }
    g_pose_stable_motion_px = vio_ai_env_get_float_default("WIDGET_POSE_STABLE_PX", 12.0f);
    if (g_pose_stable_motion_px < 4.0f) {
        g_pose_stable_motion_px = 4.0f;
    }
    if (g_pose_stable_motion_px > 40.0f) {
        g_pose_stable_motion_px = 40.0f;
    }
    g_pose_kpt_snap_px = vio_ai_env_get_float_default("WIDGET_POSE_KPT_SNAP_PX", 8.0f);
    if (g_pose_kpt_snap_px < 2.0f) {
        g_pose_kpt_snap_px = 2.0f;
    }
    if (g_pose_kpt_snap_px > 30.0f) {
        g_pose_kpt_snap_px = 30.0f;
    }
    g_pose_miss_max = (td_u32)vio_ai_env_get_int_default("WIDGET_POSE_MISS_MAX", 3);
    if (g_pose_miss_max < 1U) {
        g_pose_miss_max = 1U;
    }
    if (g_pose_miss_max > 20U) {
        g_pose_miss_max = 20U;
    }
    g_pose_clear_on_action = vio_ai_env_get_int_default("WIDGET_POSE_CLEAR_ON_ACTION", 0);
    g_pose_clear_on_swing = vio_ai_env_get_int_default("WIDGET_POSE_CLEAR_ON_SWING", 1);
    g_pose_bbox_jump_px = vio_ai_env_get_float_default("WIDGET_POSE_BBOX_JUMP_PX", 28.0f);
    if (g_pose_bbox_jump_px < 8.0f) {
        g_pose_bbox_jump_px = 8.0f;
    }
    if (g_pose_bbox_jump_px > 120.0f) {
        g_pose_bbox_jump_px = 120.0f;
    }
    g_pose_smooth_alpha = vio_ai_env_get_float_default("WIDGET_POSE_SMOOTH_ALPHA", 0.35f);
    if (g_pose_smooth_alpha < 0.10f) {
        g_pose_smooth_alpha = 0.10f;
    }
    if (g_pose_smooth_alpha > 0.90f) {
        g_pose_smooth_alpha = 0.90f;
    }
    g_pose_ch1_only = vio_ai_env_get_int_default("WIDGET_POSE_CH1_ONLY", 1);
    g_pose_rgn_enable = vio_ai_env_get_int_default("WIDGET_POSE_RGN", 0);
    g_replay_live_ring = vio_ai_env_get_int_default("WIDGET_REPLAY_LIVE", 0);
    g_yolo_draw_nv12 = vio_ai_env_get_int_default("WIDGET_YOLO_DRAW_NV12", 0);
    g_yolo_export_action = vio_ai_env_get_int_default("WIDGET_YOLO_ACTION", 1);
    {
        const char *conf_pct = getenv("WIDGET_YOLO_CONF_PERCENT");
        const char *conf_env = getenv("WIDGET_YOLO_CONF");
        float conf_th = YOLO_ACTION_CONF_DEFAULT;

        if (conf_pct != TD_NULL && conf_pct[0] != '\0') {
            int pct = atoi(conf_pct);
            if (pct >= 1 && pct <= 99) {
                conf_th = (float)pct / 100.0f;
            }
        } else if (conf_env != TD_NULL && conf_env[0] != '\0') {
            conf_th = (float)atof(conf_env);
        } else {
            conf_th = vio_ai_env_get_float_default("WIDGET_YOLO_SCORE_THRES", YOLO_ACTION_CONF_DEFAULT);
        }
        if (conf_th < 0.05f) {
            conf_th = 0.05f;
        }
        if (conf_th > 0.99f) {
            conf_th = 0.99f;
        }
        g_yolo_action_conf_thres = conf_th;
    }
    g_yolo_swing_enable = vio_ai_env_get_int_default("WIDGET_YOLO_SWING", 1);
    g_swing_cooldown_ms = vio_ai_env_get_int_default("WIDGET_YOLO_SWING_COOLDOWN_MS", YOLO_SWING_COOLDOWN_MS_DEFAULT);
    if (g_swing_cooldown_ms < 800) {
        g_swing_cooldown_ms = 800;
    }
    if (g_swing_cooldown_ms > 5000) {
        g_swing_cooldown_ms = 5000;
    }
    g_swing_vel_thres = vio_ai_env_get_float_default("WIDGET_YOLO_SWING_VEL", YOLO_SWING_VEL_THRES_DEFAULT);
    if (g_swing_vel_thres < 0.12f) {
        g_swing_vel_thres = 0.12f;
    }
    if (g_swing_vel_thres > 1.20f) {
        g_swing_vel_thres = 1.20f;
    }
    g_swing_peak_thres = vio_ai_env_get_float_default("WIDGET_YOLO_SWING_PEAK", YOLO_SWING_PEAK_THRES_DEFAULT);
    if (g_swing_peak_thres < g_swing_vel_thres + 0.06f) {
        g_swing_peak_thres = g_swing_vel_thres + 0.08f;
    }
    if (g_swing_peak_thres > 1.50f) {
        g_swing_peak_thres = 1.50f;
    }
    {
        const char *fire_pct = getenv("WIDGET_YOLO_SWING_FIRE_PERCENT");
        float fire_floor = YOLO_SWING_FIRE_SCORE_FLOOR;
        if (fire_pct != TD_NULL && fire_pct[0] != '\0') {
            int pct = atoi(fire_pct);
            if (pct >= 20 && pct <= 95) {
                fire_floor = (float)pct / 100.0f;
            }
        } else {
            fire_floor = vio_ai_env_get_float_default("WIDGET_YOLO_SWING_FIRE", YOLO_SWING_FIRE_SCORE_FLOOR);
        }
        if (fire_floor < 0.20f) {
            fire_floor = 0.20f;
        }
        if (fire_floor > 0.90f) {
            fire_floor = 0.90f;
        }
        g_swing_fire_score_floor = fire_floor;
    }
    g_serve_phase_scale = vio_ai_env_get_float_default("WIDGET_YOLO_SERVE_SCALE", YOLO_SERVE_PHASE_SCALE_DEFAULT);
    if (g_serve_phase_scale < 0.01f) {
        g_serve_phase_scale = 0.01f;
    }
    if (g_serve_phase_scale > 1.0f) {
        g_serve_phase_scale = 1.0f;
    }
    g_serve_win_ratio = vio_ai_env_get_float_default("WIDGET_YOLO_SERVE_WIN_RATIO", YOLO_SERVE_WIN_RATIO_DEFAULT);
    if (g_serve_win_ratio < 1.2f) {
        g_serve_win_ratio = 1.2f;
    }
    if (g_serve_win_ratio > 20.0f) {
        g_serve_win_ratio = 20.0f;
    }
    {
        const char *tgt = getenv("WIDGET_YOLO_TARGET");
        /* 默认 action：直接采用 YOLOv8 动作类检测框（6 类） */
        g_yolo_target_mode = YOLO_TARGET_MODE_SCORE;
        if (tgt != NULL && tgt[0] != '\0') {
            if (strcmp(tgt, "score") == 0 || strcmp(tgt, "all") == 0 ||
                strcmp(tgt, "action") == 0 || strcmp(tgt, "yolo") == 0 ||
                strcmp(tgt, "class") == 0) {
                g_yolo_target_mode = YOLO_TARGET_MODE_SCORE;
            } else if (strcmp(tgt, "hand_racket") == 0 || strcmp(tgt, "hand") == 0) {
                g_yolo_target_mode = YOLO_TARGET_MODE_HAND;
            } else if (strcmp(tgt, "person") == 0 || strcmp(tgt, "body") == 0 ||
                strcmp(tgt, "human") == 0) {
                g_yolo_target_mode = YOLO_TARGET_MODE_PERSON;
            }
        }
    }
    {
        const char *tgt_name = "action";
        if (g_yolo_target_mode == YOLO_TARGET_MODE_SCORE) {
            tgt_name = "action";
        } else if (g_yolo_target_mode == YOLO_TARGET_MODE_HAND) {
            tgt_name = "hand_racket";
        } else if (g_yolo_target_mode == YOLO_TARGET_MODE_PERSON) {
            tgt_name = "person";
        }
        printf("yolo tune: show_max=%d conf=%.0f%% smooth=%.2f/%.2f disp=%.2f@%dHz hold=%d frame_ms=%d target=%s swing_vel=%.2f peak=%.2f cd=%dms serve_scale=%.2f win_ratio=%.1f\n",
            g_yolo_show_max, g_yolo_action_conf_thres * 100.0f,
            g_yolo_smooth_alpha, g_yolo_smooth_size_alpha,
            g_yolo_display_smooth, 1000 / g_yolo_disp_interval_ms,
            g_yolo_hold_max, g_yolo_vpss_get_ms, tgt_name,
            g_swing_vel_thres, g_swing_peak_thres, g_swing_cooldown_ms,
            g_serve_phase_scale, g_serve_win_ratio);
    }
}

td_void yolo_refresh_preview_src_size(td_void)
{
    ot_vpss_chn_attr attr;

    (td_void)memset_s(&attr, sizeof(attr), 0, sizeof(attr));
    if (ss_mpi_vpss_get_chn_attr(g_attach_grp, 0, &attr) == TD_SUCCESS &&
        attr.width >= 32U && attr.height >= 32U) {
        g_preview_src_w = attr.width;
        g_preview_src_h = attr.height;
    }
}

static float yolo_iou(const yolo_det_t *a, const yolo_det_t *b);
static float yolo_track_match_score(const yolo_det_t *meas, const yolo_det_t *prev);
static td_u32 yolo_class_color_rgn(int cls_id);
static td_bool sample_vio_ai_try_attach_target(ot_vpss_grp grp, ot_vpss_chn chn,
    td_bool *depth_changed, td_u32 *old_depth);
static td_u32 overlay_auto_line_width(td_u32 map_w, td_u32 map_h)
{
    td_u32 lw;

    lw = (td_u32)(((float)map_w + (float)map_h) * 0.5f * 0.003f + 0.5f);
    if (lw < 2U) {
        lw = 2U;
    }
    if (lw > 8U) {
        lw = 8U;
    }
    return lw;
}

static float yolo_dynamic_show_thres(void)
{
    float th = g_last_top_p * YOLO_ACTION_DYNAMIC_THRES_ALPHA;
    if (th < YOLO_ACTION_DYNAMIC_THRES_MIN) th = YOLO_ACTION_DYNAMIC_THRES_MIN;
    if (th > YOLO_ACTION_DYNAMIC_THRES_MAX) th = YOLO_ACTION_DYNAMIC_THRES_MAX;
    return th;
}

static int yolo_select_action_by_class(yolo_det_t *src, int src_cnt, yolo_det_t *dst, int max_dst, int num_cls)
{
    int out = 0;
    int c;
    float th = yolo_dynamic_show_thres();
    for (c = 0; c < num_cls; c++) {
        int k;
        for (k = 0; k < YOLO_ACTION_PER_CLASS_TOPK; k++) {
            int best_i = -1;
            float best_s = th;
            int i;
            for (i = 0; i < src_cnt; i++) {
                if (src[i].cls_id != c) continue;
                if (src[i].score > best_s) {
                    best_s = src[i].score;
                    best_i = i;
                }
            }
            if (best_i >= 0 && out < max_dst) {
                dst[out++] = src[best_i];
                src[best_i].score = -fabsf(src[best_i].score); /* prevent re-pick */
            }
        }
    }
    for (c = 0; c < src_cnt; c++) {
        if (src[c].score < 0.0f) src[c].score = -src[c].score;
    }
    return out;
}

static td_void yolo_rgn_deinit(td_void)
{
    int i;
    if (g_rgn_inited != TD_TRUE) {
        return;
    }
    for (i = 0; i < YOLO_RGN_MAX; i++) {
        ot_rgn_handle h = YOLO_RGN_HANDLE_BASE + i;
        (td_void)ss_mpi_rgn_detach_from_chn(h, &g_rgn_chn);
        (td_void)ss_mpi_rgn_destroy(h);
    }
    g_rgn_inited = TD_FALSE;
}

/* Region 挂在 VO 时，以 VO 通道实际矩形为准（与 camera_pipe_vo_set_window 一致） */
td_void yolo_rgn_refresh_vo_rect(td_void)
{
    const char *vo_layer_env = getenv("WIDGET_VO_LAYER");
    td_u32 vo_layer = (vo_layer_env != NULL && vo_layer_env[0] != '\0') ?
        (td_u32)strtoul(vo_layer_env, NULL, 0) : 0U;
    ot_vo_chn_attr chn_attr;

    td_u32 ox = g_rgn_disp_ox;
    td_u32 oy = g_rgn_disp_oy;
    td_u32 w = g_rgn_disp_w;
    td_u32 h = g_rgn_disp_h;

    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
    if (ss_mpi_vo_get_chn_attr((ot_vo_layer)vo_layer, 0, &chn_attr) == TD_SUCCESS &&
        chn_attr.rect.width >= 32U && chn_attr.rect.height >= 32U) {
        ox = (td_u32)chn_attr.rect.x;
        oy = (td_u32)chn_attr.rect.y;
        w = chn_attr.rect.width;
        h = chn_attr.rect.height;
    } else {
        FILE *fp = fopen(WIDGET_CAM_VO_STATE, "re");
        int show = 0;
        int x = 0;
        int y = 0;
        int iw = 0;
        int ih = 0;
        if (fp != NULL) {
            if (fscanf(fp, " %d %d %d %d %d ", &show, &x, &y, &iw, &ih) >= 5 && show != 0 && iw > 0 && ih > 0) {
                ox = (td_u32)x;
                oy = (td_u32)y;
                w = (td_u32)iw;
                h = (td_u32)ih;
            }
            (void)fclose(fp);
        }
    }

    if (w > 1920U) {
        w = 1920U;
    }
    if (h > 1080U) {
        h = 1080U;
    }
    /* 防抖：VO 小窗几何小幅抖动时不改映射，避免框在左上角抽动 */
    if ((ox > g_rgn_disp_ox ? ox - g_rgn_disp_ox : g_rgn_disp_ox - ox) <= 4U &&
        (oy > g_rgn_disp_oy ? oy - g_rgn_disp_oy : g_rgn_disp_oy - oy) <= 4U &&
        (w > g_rgn_disp_w ? w - g_rgn_disp_w : g_rgn_disp_w - w) <= 8U &&
        (h > g_rgn_disp_h ? h - g_rgn_disp_h : g_rgn_disp_h - h) <= 8U) {
        return;
    }
    g_rgn_disp_ox = ox;
    g_rgn_disp_oy = oy;
    g_rgn_disp_w = w;
    g_rgn_disp_h = h;
}

/* Region 挂在 VPSS 预览通道 ch0 时，用传感器输出分辨率 */
td_void yolo_rgn_refresh_vpss_preview_rect(td_void)
{
    ot_vpss_chn_attr attr;
    (td_void)memset_s(&attr, sizeof(attr), 0, sizeof(attr));
    if (ss_mpi_vpss_get_chn_attr(g_attach_grp, 0, &attr) == TD_SUCCESS &&
        attr.width > 0 && attr.height > 0) {
        g_rgn_disp_ox = 0;
        g_rgn_disp_oy = 0;
        g_rgn_disp_w = attr.width;
        g_rgn_disp_h = attr.height;
    }
}

static td_s32 yolo_rgn_try_init(const ot_mpp_chn *chn, td_bool create_boxes)
{
    int i;
    td_s32 ret;

    if (chn == TD_NULL) {
        return TD_FAILURE;
    }
    g_rgn_chn = *chn;
    g_rgn_chn_ready = TD_TRUE;
    if (create_boxes == TD_FALSE) {
        return TD_SUCCESS;
    }

    for (i = 0; i < YOLO_RGN_MAX; i++) {
        ot_rgn_handle h = YOLO_RGN_HANDLE_BASE + i;
        ot_rgn_attr attr;
        ot_rgn_chn_attr chn_attr;
        ot_rgn_corner_rect_chn_attr *cr;

        (td_void)memset_s(&attr, sizeof(attr), 0, sizeof(attr));
        (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
        attr.type = OT_RGN_CORNER_RECTEX;
        ret = ss_mpi_rgn_create(h, &attr);
        if (ret != TD_SUCCESS) {
            if (ret != OT_ERR_RGN_EXIST) {
                return ret;
            }
        }
        chn_attr.is_show = TD_FALSE;
        chn_attr.type = OT_RGN_CORNER_RECTEX;
        cr = &chn_attr.attr.corner_rectex_chn;
        cr->layer = i;
        cr->corner_rect.thick = 4;
        cr->corner_rect.hor_len = 16;
        cr->corner_rect.ver_len = 16;
        cr->corner_rect_attr.color = 0x00ff00;
        cr->corner_rect_attr.corner_rect_type = OT_CORNER_RECT_TYPE_FULL_LINE;
        cr->corner_rect.rect.x = 0;
        cr->corner_rect.rect.y = 0;
        cr->corner_rect.rect.width = 32;
        cr->corner_rect.rect.height = 32;
        ret = ss_mpi_rgn_attach_to_chn(h, chn, &chn_attr);
        if ((ret != TD_SUCCESS) && (ret != OT_ERR_RGN_EXIST)) {
            return ret;
        }
    }
    g_rgn_chn = *chn;
    g_rgn_inited = TD_TRUE;
    return TD_SUCCESS;
}

td_void yolo_rgn_lazy_init(td_void)
{
    td_s32 ret;
    ot_mpp_chn chn;
    const char *vo_layer_env = getenv("WIDGET_VO_LAYER");
    td_u32 vo_layer = (vo_layer_env != NULL && vo_layer_env[0] != '\0') ?
        (td_u32)strtoul(vo_layer_env, NULL, 0) : 0U;

    if (g_rgn_chn_ready == TD_TRUE) {
        return;
    }

    yolo_rgn_refresh_vpss_preview_rect();
    yolo_refresh_preview_src_size();

    {
        const char *rgn_env = getenv("WIDGET_YOLO_RGN");
        td_bool force_vo = TD_FALSE;
        const td_bool create_boxes = (g_yolo_box_draw != 0) ? TD_TRUE : TD_FALSE;

        if (rgn_env != NULL && strcmp(rgn_env, "vo") == 0) {
            force_vo = TD_TRUE;
        }

        if (force_vo == TD_FALSE) {
            /* 优先 VPSS 预览 ch0：框随视频缩放，VO 小窗时坐标正确 */
            chn.mod_id = OT_ID_VPSS;
            chn.dev_id = g_attach_grp;
            chn.chn_id = 0;
            ret = yolo_rgn_try_init(&chn, create_boxes);
            if (ret == TD_SUCCESS) {
                if (create_boxes != TD_FALSE) {
                    printf("yolo rgn: attach VPSS(%d,0) src=%ux%u\n",
                        g_attach_grp, g_preview_src_w, g_preview_src_h);
                } else {
                    printf("rgn: display VPSS(%d,0) src=%ux%u (skeleton only)\n",
                        g_attach_grp, g_preview_src_w, g_preview_src_h);
                }
                return;
            }
            printf("yolo rgn: VPSS failed ret=0x%x, try VO...\n", (td_u32)ret);
        }
    }

    yolo_rgn_refresh_vo_rect();
    chn.mod_id = OT_ID_VO;
    chn.dev_id = vo_layer;
    chn.chn_id = 0;
    ret = yolo_rgn_try_init(&chn, (g_yolo_box_draw != 0) ? TD_TRUE : TD_FALSE);
    if (ret == TD_SUCCESS) {
        if (g_yolo_box_draw != 0) {
            printf("yolo rgn: attach VO(layer=%u,0) win=%u,%u %ux%u (abs coords)\n",
                vo_layer, g_rgn_disp_ox, g_rgn_disp_oy, g_rgn_disp_w, g_rgn_disp_h);
        } else {
            printf("rgn: display VO(layer=%u,0) win=%u,%u %ux%u (skeleton only)\n",
                vo_layer, g_rgn_disp_ox, g_rgn_disp_oy, g_rgn_disp_w, g_rgn_disp_h);
        }
        return;
    }
    printf("yolo rgn: init failed ret=0x%x\n", (td_u32)ret);
}

static td_void yolo_blend_box_with_alpha(yolo_det_t *dst, const yolo_det_t *meas, const yolo_det_t *prev,
    float a_pos, float a_sz)
{
    float cx_m = (meas->x1 + meas->x2) * 0.5f;
    float cy_m = (meas->y1 + meas->y2) * 0.5f;
    float cx_p = (prev->x1 + prev->x2) * 0.5f;
    float cy_p = (prev->y1 + prev->y2) * 0.5f;
    float w_m = meas->x2 - meas->x1;
    float h_m = meas->y2 - meas->y1;
    float w_p = prev->x2 - prev->x1;
    float h_p = prev->y2 - prev->y1;
    float cx = a_pos * cx_m + (1.0f - a_pos) * cx_p;
    float cy = a_pos * cy_m + (1.0f - a_pos) * cy_p;
    float bw = a_sz * w_m + (1.0f - a_sz) * w_p;
    float bh = a_sz * h_m + (1.0f - a_sz) * h_p;

    if (bw < 8.0f) {
        bw = 8.0f;
    }
    if (bh < 8.0f) {
        bh = 8.0f;
    }
    dst->x1 = cx - bw * 0.5f;
    dst->y1 = cy - bh * 0.5f;
    dst->x2 = cx + bw * 0.5f;
    dst->y2 = cy + bh * 0.5f;
    dst->score = a_pos * meas->score + (1.0f - a_pos) * prev->score;
    dst->cls_id = meas->cls_id;
}

static td_void yolo_publish_draw_dets(td_u32 img_w, td_u32 img_h)
{
    int i;

    pthread_mutex_lock(&g_disp_mtx);
    g_disp_target_cnt = g_draw_det_cnt;
    for (i = 0; i < g_draw_det_cnt; i++) {
        g_disp_target[i] = g_draw_dets[i];
    }
    g_disp_net_w = (img_w > 0) ? img_w : 640U;
    g_disp_net_h = (img_h > 0) ? img_h : 640U;
    pthread_mutex_unlock(&g_disp_mtx);
}

static td_void yolo_rgn_update_dets(const yolo_det_t *dets, int det_cnt, td_u32 img_w, td_u32 img_h)
{
    int i;
    td_u32 net_w;
    td_u32 net_h;
    td_u32 map_w;
    td_u32 map_h;
    td_u32 off_x = 0;
    td_u32 off_y = 0;
    float sx;
    float sy;

    if (g_yolo_box_draw == 0) {
        if (g_rgn_inited == TD_TRUE) {
            for (i = 0; i < YOLO_RGN_MAX; i++) {
                ot_rgn_handle h = YOLO_RGN_HANDLE_BASE + i;
                ot_rgn_chn_attr chn_attr;
                (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
                chn_attr.type = OT_RGN_CORNER_RECTEX;
                chn_attr.is_show = TD_FALSE;
                (td_void)ss_mpi_rgn_set_display_attr(h, &g_rgn_chn, &chn_attr);
            }
        }
        return;
    }

    if (g_rgn_inited != TD_TRUE) {
        return;
    }
    if (dets == TD_NULL || det_cnt <= 0) {
        for (i = 0; i < YOLO_RGN_MAX; i++) {
            ot_rgn_handle h = YOLO_RGN_HANDLE_BASE + i;
            ot_rgn_chn_attr chn_attr;
            ot_rgn_corner_rect_chn_attr *cr;
            (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
            chn_attr.type = OT_RGN_CORNER_RECTEX;
            chn_attr.is_show = TD_FALSE;
            cr = &chn_attr.attr.corner_rectex_chn;
            cr->layer = i;
            (td_void)ss_mpi_rgn_set_display_attr(h, &g_rgn_chn, &chn_attr);
        }
        return;
    }
    if (g_rgn_chn.mod_id == OT_ID_VO) {
        yolo_rgn_refresh_vo_rect();
        map_w = g_rgn_disp_w;
        map_h = g_rgn_disp_h;
        off_x = g_rgn_disp_ox;
        off_y = g_rgn_disp_oy;
    } else {
        yolo_rgn_refresh_vpss_preview_rect();
        yolo_refresh_preview_src_size();
        map_w = g_preview_src_w;
        map_h = g_preview_src_h;
        if (map_w < 32U || map_h < 32U) {
            map_w = g_rgn_disp_w;
            map_h = g_rgn_disp_h;
        }
    }

    net_w = (img_w > 0) ? img_w : g_rgn_net_w;
    net_h = (img_h > 0) ? img_h : g_rgn_net_h;
    if (net_w == 0) {
        net_w = 640;
    }
    if (net_h == 0) {
        net_h = 640;
    }
    sx = (float)map_w / (float)net_w;
    sy = (float)map_h / (float)net_h;
    {
        td_u32 box_thick = overlay_auto_line_width(map_w, map_h);
        td_u32 bracket_len = overlay_corner_bracket_len(map_w, map_h, box_thick);

        for (i = 0; i < YOLO_RGN_MAX; i++) {
            ot_rgn_handle h = YOLO_RGN_HANDLE_BASE + i;
            ot_rgn_chn_attr chn_attr;
            ot_rgn_corner_rect_chn_attr *cr;
            (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
            chn_attr.type = OT_RGN_CORNER_RECTEX;
            cr = &chn_attr.attr.corner_rectex_chn;
            cr->layer = i;
            cr->corner_rect.thick = box_thick;
            cr->corner_rect.hor_len = bracket_len;
            cr->corner_rect.ver_len = bracket_len;
            cr->corner_rect_attr.corner_rect_type = OT_CORNER_RECT_TYPE_FULL_LINE;
            if (i < det_cnt) {
                int x1 = (int)(dets[i].x1 * sx + 0.5f) + (int)off_x;
                int y1 = (int)(dets[i].y1 * sy + 0.5f) + (int)off_y;
                int x2 = (int)(dets[i].x2 * sx + 0.5f) + (int)off_x;
                int y2 = (int)(dets[i].y2 * sy + 0.5f) + (int)off_y;
                int w = x2 - x1;
                int hgt = y2 - y1;
                td_u32 lim_w = map_w + off_x;
                td_u32 lim_h = map_h + off_y;
                if (w < 8) {
                    w = 8;
                }
                if (hgt < 8) {
                    hgt = 8;
                }
                if (x1 < (int)off_x) {
                    x1 = (int)off_x;
                }
                if (y1 < (int)off_y) {
                    y1 = (int)off_y;
                }
                if ((td_u32)(x1 + w) > lim_w) {
                    w = (int)lim_w - x1;
                }
                if ((td_u32)(y1 + hgt) > lim_h) {
                    hgt = (int)lim_h - y1;
                }
                chn_attr.is_show = TD_TRUE;
                cr->corner_rect_attr.color = yolo_class_color_rgn(dets[i].cls_id);
                cr->corner_rect.rect.x = x1;
                cr->corner_rect.rect.y = y1;
                cr->corner_rect.rect.width = (td_u32)w;
                cr->corner_rect.rect.height = (td_u32)hgt;
            } else {
                chn_attr.is_show = TD_FALSE;
                cr->corner_rect_attr.color = yolo_class_color_rgn(0);
                cr->corner_rect.rect.x = 0;
                cr->corner_rect.rect.y = 0;
                cr->corner_rect.rect.width = 8;
                cr->corner_rect.rect.height = 8;
            }
            (td_void)ss_mpi_rgn_set_display_attr(h, &g_rgn_chn, &chn_attr);
        }
    }
}

static td_void yolo_rgn_update(td_u32 img_w, td_u32 img_h)
{
    yolo_rgn_update_dets(g_draw_dets, g_draw_det_cnt, img_w, img_h);
}

/* YOLOv8 pose skeleton moved to vio_ai_pose.c */

#define RGN_RGB(r, g, b) (((td_u32)(r) << 16) | ((td_u32)(g) << 8) | (td_u32)(b))

td_u32 overlay_corner_bracket_len(td_u32 map_w, td_u32 map_h, td_u32 thick)
{
    td_u32 ref;
    td_u32 len = thick * 4U;

    ref = (map_w < map_h) ? (map_w / 20U) : (map_h / 20U);
    if (len < 8U) {
        len = 8U;
    }
    if (ref > len && ref < 48U) {
        len = ref;
    }
    return len;
}

static td_u32 yolo_class_color_rgn(int cls_id)
{
    /* YOLOs-CPP generateColors：按类别稳定着色 */
    static const td_u32 palette[] = {
        RGN_RGB(255, 128, 0), RGN_RGB(0, 200, 255), RGN_RGB(255, 64, 160),
        RGN_RGB(64, 255, 128), RGN_RGB(255, 220, 64), RGN_RGB(160, 96, 255),
        RGN_RGB(255, 96, 96), RGN_RGB(96, 255, 255)
    };

    if (cls_id < 0) {
        cls_id = 0;
    }
    return palette[cls_id % (int)(sizeof(palette) / sizeof(palette[0]))];
}

static td_void *yolo_display_thread(td_void *arg)
{
    yolo_det_t show[YOLO_MAX_DET];
    yolo_det_t target[YOLO_MAX_DET];
    int show_n;
    int target_n;
    int i;
    td_u32 net_w;
    td_u32 net_h;
    static td_u32 disp_tick = 0;

    (void)arg;
    while (g_disp_thread_run != 0) {
        usleep((useconds_t)g_yolo_disp_interval_ms * 1000U);
        if (g_rgn_inited != TD_TRUE) {
            continue;
        }

        pthread_mutex_lock(&g_disp_mtx);
        target_n = g_disp_target_cnt;
        for (i = 0; i < target_n; i++) {
            target[i] = g_disp_target[i];
        }
        net_w = g_disp_net_w;
        net_h = g_disp_net_h;
        show_n = g_disp_show_cnt;
        for (i = 0; i < show_n; i++) {
            show[i] = g_disp_show[i];
        }

        if (target_n > 0) {
            float a = g_yolo_display_smooth;
            float a_sz = a * 0.75f;
            if (show_n <= 0) {
                for (i = 0; i < target_n; i++) {
                    g_disp_show[i] = target[i];
                }
                g_disp_show_cnt = target_n;
            } else {
                int n = (target_n < show_n) ? target_n : show_n;
                for (i = 0; i < n; i++) {
                    yolo_blend_box_with_alpha(&g_disp_show[i], &target[i], &show[i], a, a_sz);
                }
                g_disp_show_cnt = n;
            }
            show_n = g_disp_show_cnt;
            for (i = 0; i < show_n; i++) {
                show[i] = g_disp_show[i];
            }
        } else {
            g_disp_show_cnt = 0;
            show_n = 0;
        }
        pthread_mutex_unlock(&g_disp_mtx);

        yolo_rgn_update_dets((show_n > 0) ? show : TD_NULL, show_n, net_w, net_h);

        disp_tick++;
        if ((disp_tick % (td_u32)(1000 / g_yolo_disp_interval_ms)) == 0) {
            printf("yolo display refresh: ~%dHz show_n=%d\n",
                1000 / g_yolo_disp_interval_ms, show_n);
        }
    }
    return TD_NULL;
}

static td_void yolo_display_thread_start(td_void)
{
    if (g_yolo_use_disp_thread == 0) {
        return;
    }
    g_disp_thread_run = 1;
    g_disp_show_cnt = 0;
    g_disp_target_cnt = 0;
    if (pthread_create(&g_disp_thread, TD_NULL, yolo_display_thread, TD_NULL) != 0) {
        printf("yolo display thread create failed, fallback sync rgn\n");
        g_disp_thread_run = 0;
        g_yolo_use_disp_thread = 0;
    } else {
        printf("yolo display thread started: %dms (~%dHz)\n",
            g_yolo_disp_interval_ms, 1000 / g_yolo_disp_interval_ms);
    }
}

static td_void yolo_display_thread_stop(td_void)
{
    if (g_disp_thread_run == 0) {
        return;
    }
    g_disp_thread_run = 0;
    (void)pthread_join(g_disp_thread, TD_NULL);
}

/* 动作模型：过滤全屏/竖条等异常框，避免误选 Serve 假框 */
static td_bool yolo_action_box_valid(const yolo_det_t *d, float img_w, float img_h)
{
    float bw;
    float bh;
    float area_ratio;
    float w_ratio;
    float h_ratio;
    float ar;

    if (d == TD_NULL || img_w < 1.0f || img_h < 1.0f) {
        return TD_FALSE;
    }
    bw = d->x2 - d->x1;
    bh = d->y2 - d->y1;
    if ((bw < 12.0f) || (bh < 12.0f)) {
        return TD_FALSE;
    }
    area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
    w_ratio = bw / img_w;
    h_ratio = bh / img_h;
    ar = bw / (bh + 1e-6f);
    if (area_ratio > YOLO_ACTION_BOX_MAX_AREA || area_ratio < YOLO_ACTION_BOX_MIN_AREA) {
        return TD_FALSE;
    }
    if (w_ratio > 0.92f && h_ratio > 0.88f) {
        return TD_FALSE;
    }
    if (w_ratio < 0.18f && h_ratio > 0.82f) {
        return TD_FALSE;
    }
    if (area_ratio > 0.35f && (ar > 2.8f || ar < 0.22f)) {
        return TD_FALSE;
    }
    if (w_ratio > 0.80f && h_ratio > 0.78f) {
        return TD_FALSE;
    }
    if (yolo_action_box_is_false_serve_blob(d, img_w, img_h) == TD_TRUE) {
        return TD_FALSE;
    }
    return TD_TRUE;
}

/* 运动跟踪：优先合理框，无则仍用 NMS 最高分框（保证挥拍速度可算） */
static int yolo_pick_motion_box(const yolo_det_t *nms, int ncnt, float fw, float fh, yolo_det_t *out)
{
    int i;
    int best_i = -1;
    float best_rank = -1.0f;

    if (nms == TD_NULL || out == TD_NULL || ncnt <= 0) {
        return 0;
    }
    if (fw < 1.0f) {
        fw = 640.0f;
    }
    if (fh < 1.0f) {
        fh = 640.0f;
    }
    for (i = 0; i < ncnt; i++) {
        float bw = nms[i].x2 - nms[i].x1;
        float bh = nms[i].y2 - nms[i].y1;
        float rank = nms[i].score;

        if (bw < 16.0f || bh < 16.0f) {
            continue;
        }
        if (yolo_action_box_valid(&nms[i], fw, fh) == TD_TRUE) {
            rank *= 1.45f;
        } else if (yolo_action_box_is_false_serve_blob(&nms[i], fw, fh) == TD_TRUE) {
            rank *= 1.10f;
        }
        if (nms[i].cls_id != YOLO_SERVE_CLASS_ID) {
            rank *= 1.15f;
        }
        if (rank > best_rank) {
            best_rank = rank;
            best_i = i;
        }
    }
    if (best_i < 0) {
        best_i = 0;
    }
    *out = nms[best_i];
    return 1;
}

static float yolo_action_box_quality_mul(const yolo_det_t *d, float img_w, float img_h)
{
    float bw;
    float bh;
    float area_ratio;
    float w_ratio;
    float h_ratio;

    if (yolo_action_box_valid(d, img_w, img_h) == TD_FALSE) {
        return 0.05f;
    }
    bw = d->x2 - d->x1;
    bh = d->y2 - d->y1;
    area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
    w_ratio = bw / img_w;
    h_ratio = bh / img_h;
    if (w_ratio > 0.75f && h_ratio > 0.70f) {
        return 0.35f;
    }
    if (area_ratio >= 0.06f && area_ratio <= 0.48f) {
        return 1.20f;
    }
    return 1.0f;
}

/* 0=人体大框 1=中等 2=手/球拍等小目标（仅 person/hand 模式） */
static int yolo_classify_target_tier(float bw, float bh, float img_w, float img_h)
{
    float area_ratio;
    float h_ratio;
    float w_ratio;
    float ar;

    if ((bw < 8.0f) || (bh < 8.0f) || (img_w < 1.0f) || (img_h < 1.0f)) {
        return 2;
    }
    area_ratio = (bw * bh) / (img_w * img_h + 1e-6f);
    h_ratio = bh / img_h;
    w_ratio = bw / img_w;
    ar = bw / (bh + 1e-6f);

    if (area_ratio >= YOLO_TIER_PERSON_AREA_MIN &&
        h_ratio >= YOLO_TIER_PERSON_H_RATIO &&
        w_ratio >= YOLO_TIER_PERSON_W_RATIO &&
        ar >= YOLO_TIER_PERSON_AR_MIN && ar <= YOLO_TIER_PERSON_AR_MAX) {
        return 0;
    }
    if (area_ratio >= YOLO_SHOW_MIN_AREA_RATIO &&
        area_ratio <= YOLO_TIER_SMALL_AREA_MAX &&
        h_ratio <= 0.58f && w_ratio <= 0.58f) {
        return 2;
    }
    return 1;
}

static float yolo_target_display_rank(const yolo_det_t *d, float img_w, float img_h)
{
    float bw = d->x2 - d->x1;
    float bh = d->y2 - d->y1;
    float ar;
    float mul;
    int tier;

    if (g_yolo_target_mode == YOLO_TARGET_MODE_SCORE) {
        float rank = d->score * yolo_action_box_quality_mul(d, img_w, img_h);
        if (d->cls_id == YOLO_SERVE_CLASS_ID) {
            rank *= yolo_action_swing_cls_weight(YOLO_SERVE_CLASS_ID, d, img_w, img_h);
            rank *= g_serve_phase_scale;
        } else {
            rank *= YOLO_NON_SERVE_BOOST;
        }
        return rank;
    }
    tier = yolo_classify_target_tier(bw, bh, img_w, img_h);
    ar = bw / (bh + 1e-6f);

    if (g_yolo_target_mode == YOLO_TARGET_MODE_PERSON) {
        switch (tier) {
            case 0:
                mul = 2.8f;
                break;
            case 1:
                mul = 1.0f;
                break;
            default:
                mul = 0.25f;
                break;
        }
        if (tier == 0 && ar >= 0.28f && ar <= 0.72f) {
            mul *= 1.18f;
        }
    } else {
        switch (tier) {
            case 2:
                mul = 2.8f;
                break;
            case 1:
                mul = 1.1f;
                break;
            default:
                mul = 0.30f;
                break;
        }
        if (tier == 2 && (ar >= 1.35f || ar <= 0.72f)) {
            mul *= 1.20f;
        }
    }
    return d->score * mul;
}

static int yolo_select_for_display(const yolo_det_t *src, int src_cnt, yolo_det_t *dst, int max_dst, td_u32 img_w, td_u32 img_h)
{
    int i;
    int j;
    int out = 0;
    int pick_max;
    float fw = (float)img_w;
    float fh = (float)img_h;

    yolo_tune_init_once();
    if (src_cnt <= 0 || max_dst <= 0) {
        return 0;
    }
    if (fw < 1.0f) {
        fw = 640.0f;
    }
    if (fh < 1.0f) {
        fh = 640.0f;
    }

    pick_max = g_yolo_show_max;
    if (pick_max > max_dst) {
        pick_max = max_dst;
    }

    /* action/score：YOLOv8 置信度最高；person/hand：几何优选 */
    for (i = 0; i < pick_max; i++) {
        int best_i = -1;
        float best_rank = -1.0f;

        for (j = 0; j < src_cnt; j++) {
            float rank;
            int tier;
            int k;
            td_bool picked = TD_FALSE;
            float bw;
            float bh;

            for (k = 0; k < out; k++) {
                if (dst[k].x1 == src[j].x1 && dst[k].y1 == src[j].y1 &&
                    dst[k].x2 == src[j].x2 && dst[k].y2 == src[j].y2) {
                    picked = TD_TRUE;
                    break;
                }
            }
            if (picked == TD_TRUE) {
                continue;
            }
            if (src[j].score < g_yolo_action_conf_thres) {
                continue;
            }
            bw = src[j].x2 - src[j].x1;
            bh = src[j].y2 - src[j].y1;
            if ((bw < 8.0f) || (bh < 8.0f)) {
                continue;
            }
            if (g_yolo_target_mode == YOLO_TARGET_MODE_SCORE &&
                yolo_action_box_valid(&src[j], fw, fh) == TD_FALSE) {
                continue;
            }
            tier = yolo_classify_target_tier(bw, bh, fw, fh);
            if (i > 0 && out > 0) {
                int t0 = yolo_classify_target_tier(dst[0].x2 - dst[0].x1, dst[0].y2 - dst[0].y1, fw, fh);
                if (g_yolo_target_mode == YOLO_TARGET_MODE_PERSON && t0 == 0 && tier == 2) {
                    continue;
                }
                if (g_yolo_target_mode == YOLO_TARGET_MODE_HAND && t0 == 2 && tier == 0) {
                    continue;
                }
            }
            rank = yolo_target_display_rank(&src[j], fw, fh);
            if (g_prev_show_cnt > 0) {
                float stick = yolo_track_match_score(&src[j], &g_prev_show_dets[0]);
                rank *= (1.0f + 1.2f * stick);
            }
            if (rank > best_rank) {
                best_rank = rank;
                best_i = j;
            }
        }
        if (best_i < 0) {
            break;
        }
        dst[out++] = src[best_i];
    }
    return out;
}

static void yolo_dump_nv21_stats(const unsigned char *nv21, td_u32 w, td_u32 h)
{
    td_u32 i;
    td_u32 y_sz = w * h;
    const unsigned char *y = nv21;
    const unsigned char *vu = nv21 + y_sz;
    unsigned int y_sum = 0;
    unsigned int v_sum = 0;
    unsigned int u_sum = 0;
    unsigned char y_min = 255, y_max = 0;
    unsigned char v_min = 255, v_max = 0;
    unsigned char u_min = 255, u_max = 0;

    if (nv21 == TD_NULL || w == 0 || h == 0) {
        return;
    }
    for (i = 0; i < y_sz; i++) {
        unsigned char v = y[i];
        y_sum += v;
        if (v < y_min) y_min = v;
        if (v > y_max) y_max = v;
    }
    for (i = 0; i < y_sz / 2; i += 2) {
        unsigned char vv = vu[i + 0];
        unsigned char uu = vu[i + 1];
        v_sum += vv;
        u_sum += uu;
        if (vv < v_min) v_min = vv;
        if (vv > v_max) v_max = vv;
        if (uu < u_min) u_min = uu;
        if (uu > u_max) u_max = uu;
    }
    printf("yolo input nv21 stats: %ux%u Y[min,max,mean]=[%u,%u,%.1f] V[min,max,mean]=[%u,%u,%.1f] U[min,max,mean]=[%u,%u,%.1f]\n",
        w, h,
        (unsigned)y_min, (unsigned)y_max, (double)y_sum / (double)y_sz,
        (unsigned)v_min, (unsigned)v_max, (double)v_sum / (double)(y_sz / 2),
        (unsigned)u_min, (unsigned)u_max, (double)u_sum / (double)(y_sz / 2));

    printf("yolo input nv21 head Y[0..15]=");
    for (i = 0; i < 16 && i < y_sz; i++) {
        printf("%02x%s", (unsigned)y[i], (i + 1 == 16) ? "" : " ");
    }
    printf(" VU[0..15]=");
    for (i = 0; i < 16 && i < y_sz / 2; i++) {
        printf("%02x%s", (unsigned)vu[i], (i + 1 == 16) ? "" : " ");
    }
    printf("\n");
}

static td_void yolo_track_reset(td_void)
{
    (td_void)memset_s(g_track_slots, sizeof(g_track_slots), 0, sizeof(g_track_slots));
    g_action_hist_len = 0;
    g_action_hist_pos = 0;
    (void)memset_s(g_action_cls_hist, sizeof(g_action_cls_hist), 0, sizeof(g_action_cls_hist));
    (void)memset_s(g_action_hist_score, sizeof(g_action_hist_score), 0, sizeof(g_action_hist_score));
    yolo_swing_reset();
}

static float yolo_track_match_score(const yolo_det_t *meas, const yolo_det_t *prev)
{
    float iou = yolo_iou(meas, prev);
    float cx_m = (meas->x1 + meas->x2) * 0.5f;
    float cy_m = (meas->y1 + meas->y2) * 0.5f;
    float cx_p = (prev->x1 + prev->x2) * 0.5f;
    float cy_p = (prev->y1 + prev->y2) * 0.5f;
    float bw = prev->x2 - prev->x1;
    float bh = prev->y2 - prev->y1;
    float dist;
    float diag;
    float dist_n;

    if (bw < 8.0f) {
        bw = 8.0f;
    }
    if (bh < 8.0f) {
        bh = 8.0f;
    }
    dist = (float)sqrt((double)((cx_m - cx_p) * (cx_m - cx_p) + (cy_m - cy_p) * (cy_m - cy_p)));
    diag = (float)sqrt((double)(bw * bw + bh * bh));
    dist_n = 1.0f - dist / (diag + 1e-6f);
    if (dist_n < 0.0f) {
        dist_n = 0.0f;
    }
    return 0.55f * iou + 0.45f * dist_n;
}

static float yolo_track_limit_step(float cur, float target, float max_step)
{
    float d = target - cur;
    if (d > max_step) {
        return cur + max_step;
    }
    if (d < -max_step) {
        return cur - max_step;
    }
    return target;
}

static td_void yolo_track_blend_box(yolo_det_t *dst, const yolo_det_t *meas, const yolo_det_t *prev)
{
    float a_pos = g_yolo_smooth_alpha;
    float a_sz = g_yolo_smooth_size_alpha;
    float cx_m = (meas->x1 + meas->x2) * 0.5f;
    float cy_m = (meas->y1 + meas->y2) * 0.5f;
    float cx_p = (prev->x1 + prev->x2) * 0.5f;
    float cy_p = (prev->y1 + prev->y2) * 0.5f;
    float w_m = meas->x2 - meas->x1;
    float h_m = meas->y2 - meas->y1;
    float w_p = prev->x2 - prev->x1;
    float h_p = prev->y2 - prev->y1;
    float cx;
    float cy;
    float bw;
    float bh;
    float jump;
    float jump_r;

    if (w_m < 8.0f) {
        w_m = 8.0f;
    }
    if (h_m < 8.0f) {
        h_m = 8.0f;
    }
    if (w_p < 8.0f) {
        w_p = 8.0f;
    }
    if (h_p < 8.0f) {
        h_p = 8.0f;
    }

    jump = (float)sqrt((double)((cx_m - cx_p) * (cx_m - cx_p) + (cy_m - cy_p) * (cy_m - cy_p)));
    jump_r = jump / ((w_p + h_p) * 0.5f + 1e-6f);
    if (jump_r > 0.85f) {
        a_pos = 0.10f;
        a_sz = 0.08f;
    } else if (jump_r > 0.40f) {
        a_pos *= 0.50f;
        a_sz *= 0.55f;
    }

    yolo_blend_box_with_alpha(dst, meas, prev, a_pos, a_sz);
    cx = (dst->x1 + dst->x2) * 0.5f;
    cy = (dst->y1 + dst->y2) * 0.5f;
    bw = dst->x2 - dst->x1;
    bh = dst->y2 - dst->y1;
    cx = yolo_track_limit_step(cx_p, cx, bw * YOLO_TRACK_MAX_CENTER_STEP);
    cy = yolo_track_limit_step(cy_p, cy, bh * YOLO_TRACK_MAX_CENTER_STEP);
    bw = yolo_track_limit_step(w_p, bw, w_p * YOLO_TRACK_MAX_SIZE_STEP);
    bh = yolo_track_limit_step(h_p, bh, h_p * YOLO_TRACK_MAX_SIZE_STEP);
    if (bw < 8.0f) {
        bw = 8.0f;
    }
    if (bh < 8.0f) {
        bh = 8.0f;
    }
    dst->x1 = cx - bw * 0.5f;
    dst->y1 = cy - bh * 0.5f;
    dst->x2 = cx + bw * 0.5f;
    dst->y2 = cy + bh * 0.5f;
}

/*
 * 双槽位跨帧跟踪：最多 2 个主目标，IOU 匹配 + EMA 平滑 + 丢检保持
 */
static int yolo_track_update(const yolo_det_t *meas, int meas_cnt, yolo_det_t *out, int max_out)
{
    int i;
    int j;
    int out_n = 0;
    td_bool meas_used[YOLO_MAX_DET];
    td_bool slot_matched[YOLO_TRACK_SLOTS];

    yolo_tune_init_once();
    if (meas_cnt > YOLO_MAX_DET) {
        meas_cnt = YOLO_MAX_DET;
    }
    if (max_out > YOLO_TRACK_SLOTS) {
        max_out = YOLO_TRACK_SLOTS;
    }

    (td_void)memset_s(meas_used, sizeof(meas_used), 0, sizeof(meas_used));
    (td_void)memset_s(slot_matched, sizeof(slot_matched), 0, sizeof(slot_matched));

    /* 1) 已有轨迹 ← 本帧检测（IOU+中心距离匹配，更易跟住同一人） */
    for (j = 0; j < YOLO_TRACK_SLOTS; j++) {
        int best_i = -1;
        float best_score = g_yolo_track_iou;
        int k;

        if (g_track_slots[j].hits <= 0) {
            continue;
        }
        if (g_track_slots[j].missed > YOLO_TRACK_MAX_AGE) {
            g_track_slots[j].hits = 0;
            g_track_slots[j].missed = 0;
            continue;
        }
        for (k = 0; k < meas_cnt; k++) {
            float ms;
            if (meas_used[k] == TD_TRUE) {
                continue;
            }
            ms = yolo_track_match_score(&meas[k], &g_track_slots[j].box);
            if (ms > best_score) {
                best_score = ms;
                best_i = k;
            }
        }
        if (best_i >= 0) {
            yolo_det_t blended;
            yolo_track_blend_box(&blended, &meas[best_i], &g_track_slots[j].box);
            g_track_slots[j].box = blended;
            g_track_slots[j].missed = 0;
            g_track_slots[j].hits++;
            meas_used[best_i] = TD_TRUE;
            slot_matched[j] = TD_TRUE;
        }
    }

    /* 2) 未匹配轨迹：missed++，在 hold 窗口内继续显示上一位置 */
    for (j = 0; j < YOLO_TRACK_SLOTS; j++) {
        if (g_track_slots[j].hits <= 0) {
            continue;
        }
        if (slot_matched[j] != TD_TRUE) {
            g_track_slots[j].missed++;
        }
    }

    /* 3) 剩余检测 → 仅无活跃轨迹时建新轨迹，且需与上一帧框相近 */
    for (i = 0; i < meas_cnt; i++) {
        int a;
        td_bool block_new = TD_FALSE;

        if (meas_used[i] == TD_TRUE) {
            continue;
        }
        for (a = 0; a < YOLO_TRACK_SLOTS; a++) {
            if (g_track_slots[a].hits > 0 && g_track_slots[a].missed <= g_yolo_hold_max) {
                block_new = TD_TRUE;
                break;
            }
        }
        if (block_new == TD_TRUE) {
            continue;
        }
        if (g_prev_show_cnt > 0) {
            if (yolo_track_match_score(&meas[i], &g_prev_show_dets[0]) < 0.08f) {
                continue;
            }
        }
        for (j = 0; j < YOLO_TRACK_SLOTS; j++) {
            if (g_track_slots[j].hits > 0) {
                continue;
            }
            g_track_slots[j].box = meas[i];
            g_track_slots[j].hits = 1;
            g_track_slots[j].missed = 0;
            meas_used[i] = TD_TRUE;
            break;
        }
    }

    /* 4) 输出：活跃轨迹（含短时 hold） */
    for (j = 0; j < YOLO_TRACK_SLOTS && out_n < max_out; j++) {
        if (g_track_slots[j].hits <= 0) {
            continue;
        }
        if (g_track_slots[j].missed > g_yolo_hold_max) {
            g_track_slots[j].hits = 0;
            g_track_slots[j].missed = 0;
            continue;
        }
        out[out_n++] = g_track_slots[j].box;
    }

    g_prev_show_cnt = (out_n < YOLO_MAX_DET) ? out_n : YOLO_MAX_DET;
    for (i = 0; i < g_prev_show_cnt; i++) {
        g_prev_show_dets[i] = out[i];
    }
    return out_n;
}

static float yolo_clamp(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float yolo_sigmoid(float x)
{
    if (x >= 0.0f) {
        float z = (float)exp(-x);
        return 1.0f / (1.0f + z);
    }
    {
        float z = (float)exp(x);
        return z / (1.0f + z);
    }
}

float yolo_prob(float x)
{
    if ((x >= 0.0f) && (x <= 1.0f)) {
        return x;
    }
    return yolo_sigmoid(x);
}

static float yolo_iou(const yolo_det_t *a, const yolo_det_t *b)
{
    float xx1 = (a->x1 > b->x1) ? a->x1 : b->x1;
    float yy1 = (a->y1 > b->y1) ? a->y1 : b->y1;
    float xx2 = (a->x2 < b->x2) ? a->x2 : b->x2;
    float yy2 = (a->y2 < b->y2) ? a->y2 : b->y2;
    float w = xx2 - xx1;
    float h = yy2 - yy1;
    float inter;
    float area_a;
    float area_b;
    float uni;

    if ((w <= 0.0f) || (h <= 0.0f)) {
        return 0.0f;
    }
    inter = w * h;
    area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
    area_b = (b->x2 - b->x1) * (b->y2 - b->y1);
    uni = area_a + area_b - inter;
    if (uni <= 1e-6f) {
        return 0.0f;
    }
    return inter / uni;
}

static void yolo_sort_desc(yolo_det_t *dets, int det_cnt)
{
    int i;
    int j;
    for (i = 0; i < det_cnt - 1; i++) {
        for (j = i + 1; j < det_cnt; j++) {
            if (dets[j].score > dets[i].score) {
                yolo_det_t tmp = dets[i];
                dets[i] = dets[j];
                dets[j] = tmp;
            }
        }
    }
}

int yolo_nms(yolo_det_t *src, int src_cnt, yolo_det_t *dst, int max_dst, float iou_thres)
{
    int keep_cnt = 0;
    int i;
    int j;
    td_bool suppressed[YOLO_MAX_DET] = {TD_FALSE};

    yolo_sort_desc(src, src_cnt);
    for (i = 0; i < src_cnt; i++) {
        if (suppressed[i] == TD_TRUE) {
            continue;
        }
        if (keep_cnt < max_dst) {
            dst[keep_cnt++] = src[i];
        }
        for (j = i + 1; j < src_cnt; j++) {
            if (suppressed[j] == TD_TRUE) {
                continue;
            }
            if (src[i].cls_id != src[j].cls_id) {
                continue;
            }
            if (yolo_iou(&src[i], &src[j]) > iou_thres) {
                suppressed[j] = TD_TRUE;
            }
        }
    }
    return keep_cnt;
}

static td_bool yolo_pick_layout(const float *out_data, size_t out_float_num, int *feat_num, int *anchor_num, td_bool *feat_first)
{
    (td_void)out_data;
    /* Prefer YOLOv8 classic [1,84,8400], fallback to [1,8400,84] / 5-class [1,10,8400]. */
    if ((out_float_num % 84) == 0) {
        *feat_num = 84;
        *anchor_num = (int)(out_float_num / 84);
        *feat_first = TD_TRUE;
        return TD_TRUE;
    }
    if ((out_float_num % 85) == 0) {
        *feat_num = 85;
        *anchor_num = (int)(out_float_num / 85);
        *feat_first = TD_TRUE;
        return TD_TRUE;
    }
    if ((out_float_num % 10) == 0) {
        *feat_num = 10;
        *anchor_num = (int)(out_float_num / 10);
        *feat_first = TD_TRUE;
        return TD_TRUE;
    }
    if ((out_float_num % 9) == 0) {
        *feat_num = 9;
        *anchor_num = (int)(out_float_num / 9);
        *feat_first = TD_TRUE;
        return TD_TRUE;
    }
    return TD_FALSE;
}

static float fp16_to_fp32(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) & 0x1;
    uint32_t exp = (uint32_t)(h >> 10) & 0x1f;
    uint32_t frac = (uint32_t)h & 0x3ff;
    uint32_t f_bits;
    float out;

    if (exp == 0) {
        if (frac == 0) {
            f_bits = sign << 31;
        } else {
            uint32_t e = 127 - 15 + 1;
            while ((frac & 0x400) == 0) {
                frac <<= 1;
                e--;
            }
            frac &= 0x3ff;
            f_bits = (sign << 31) | (e << 23) | (frac << 13);
        }
    } else if (exp == 31) {
        f_bits = (sign << 31) | 0x7f800000 | (frac << 13);
    } else {
        uint32_t e = exp + (127 - 15);
        f_bits = (sign << 31) | (e << 23) | (frac << 13);
    }
    (td_void)memcpy(&out, &f_bits, sizeof(out));
    return out;
}

static size_t acl_dtype_size(aclDataType dtype)
{
    switch (dtype) {
        case ACL_FLOAT16:
            return 2;
        case ACL_FLOAT:
            return 4;
        case ACL_INT8:
        case ACL_UINT8:
            return 1;
        case ACL_INT16:
        case ACL_UINT16:
            return 2;
        case ACL_INT32:
        case ACL_UINT32:
            return 4;
        default:
            return 0;
    }
}

static td_bool convert_output_to_fp32(const void *src, size_t src_size, aclDataType dtype, float **dst, size_t *dst_num)
{
    size_t elem_size;
    size_t i;
    float *buf;

    *dst = TD_NULL;
    *dst_num = 0;

    elem_size = acl_dtype_size(dtype);
    if ((elem_size == 0) || (src_size < elem_size) || ((src_size % elem_size) != 0)) {
        return TD_FALSE;
    }
    *dst_num = src_size / elem_size;
    buf = (float *)malloc((*dst_num) * sizeof(float));
    if (buf == TD_NULL) {
        *dst_num = 0;
        return TD_FALSE;
    }

    if (dtype == ACL_FLOAT) {
        (td_void)memcpy(buf, src, (*dst_num) * sizeof(float));
    } else if (dtype == ACL_FLOAT16) {
        const uint16_t *p = (const uint16_t *)src;
        for (i = 0; i < *dst_num; i++) {
            buf[i] = fp16_to_fp32(p[i]);
        }
    } else {
        free(buf);
        *dst = TD_NULL;
        *dst_num = 0;
        return TD_FALSE;
    }

    *dst = buf;
    return TD_TRUE;
}

static td_bool yolo_get_model_input_wh(td_u32 *net_w, td_u32 *net_h)
{
    aclmdlIODims dims;
    td_s32 ret;
    td_u32 a;
    td_u32 b;

    if ((net_w == TD_NULL) || (net_h == TD_NULL)) {
        return TD_FALSE;
    }
    *net_w = 640;
    *net_h = 640;
    (td_void)memset_s(&dims, sizeof(dims), 0, sizeof(dims));
    ret = aclmdlGetInputDims(g_model_desc, 0, &dims);
    if ((ret != ACL_SUCCESS) || (dims.dimCount < 4)) {
        return TD_FALSE;
    }
    a = (td_u32)dims.dims[dims.dimCount - 2];
    b = (td_u32)dims.dims[dims.dimCount - 1];
    if ((a >= b) && (a >= 64)) {
        *net_h = a;
        *net_w = b;
    } else {
        *net_h = b;
        *net_w = a;
    }
    if ((*net_w < 64) || (*net_h < 64)) {
        *net_w = 640;
        *net_h = 640;
        return TD_FALSE;
    }
    return TD_TRUE;
}


static void yolo_try_dump_nv21_once(const unsigned char *nv21, td_u32 w, td_u32 h)
{
#if YOLO_DUMP_INPUT_NV21
    static td_bool dumped = TD_FALSE;
    FILE *f;
    size_t sz;
    const char *dump_path;
    if ((dumped == TD_TRUE) || (nv21 == TD_NULL) || (w == 0) || (h == 0)) {
        return;
    }
    dumped = TD_TRUE;
    sz = (size_t)w * h * 3 / 2;
    /* Our input buffer is either NV12 or NV21 depending on g_ab_feed_nv12.
       This dump is used only for local visualization/debug, so name it accordingly. */
    dump_path = (g_ab_feed_nv12 != 0) ? "/tmp/aipp_in_640x640_nv12.yuv" : "/tmp/aipp_in_640x640_nv21.yuv";
    f = fopen(dump_path, "wb");
    if (f == TD_NULL) {
        return;
    }
    (void)fwrite(nv21, 1, sz, f);
    (void)fclose(f);
    printf("yolo dump: wrote %s (%zu bytes)\n", dump_path, sz);
#else
    (void)nv21;
    (void)w;
    (void)h;
#endif
}

static unsigned char yolo_u8_clip(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (unsigned char)v;
}

static void yolo_dump_rgb_chw_stats(const float *chw, td_u32 w, td_u32 h)
{
    size_t n = (size_t)w * h;
    size_t i;
    const float *r = chw;
    const float *g = chw + n;
    const float *b = chw + n * 2;
    float mn = 1e9f;
    float mx = -1e9f;
    double sum = 0.0;
    double sum2 = 0.0;
    double rs = 0.0;
    double gs = 0.0;
    double bs = 0.0;
    for (i = 0; i < n; i++) {
        float rv = r[i];
        float gv = g[i];
        float bv = b[i];
        if (rv < mn) mn = rv;
        if (gv < mn) mn = gv;
        if (bv < mn) mn = bv;
        if (rv > mx) mx = rv;
        if (gv > mx) mx = gv;
        if (bv > mx) mx = bv;
        rs += rv;
        gs += gv;
        bs += bv;
        sum += (rv + gv + bv);
        sum2 += (double)rv * rv + (double)gv * gv + (double)bv * bv;
    }
    {
        double m = sum / (double)(n * 3);
        double v = sum2 / (double)(n * 3) - m * m;
        if (v < 0.0) v = 0.0;
        printf("yolo input rgb stats: min=%.6f max=%.6f mean=%.6f std=%.6f R_mean=%.6f G_mean=%.6f B_mean=%.6f\n",
            mn, mx, (float)m, (float)sqrt(v), (float)(rs / (double)n), (float)(gs / (double)n), (float)(bs / (double)n));
    }
}

static void yolo_bgr_mean_u8(const unsigned char *bgr, td_u32 w, td_u32 h, float *r_mean, float *g_mean, float *b_mean)
{
    size_t i;
    size_t n = (size_t)w * h;
    double rs = 0.0;
    double gs = 0.0;
    double bs = 0.0;
    for (i = 0; i < n; i++) {
        bs += (double)bgr[i * 3 + 0] * (1.0 / 255.0);
        gs += (double)bgr[i * 3 + 1] * (1.0 / 255.0);
        rs += (double)bgr[i * 3 + 2] * (1.0 / 255.0);
    }
    *r_mean = (float)(rs / (double)n);
    *g_mean = (float)(gs / (double)n);
    *b_mean = (float)(bs / (double)n);
}

static void yolo_yuv420sp_to_bgr_u8(const unsigned char *src, td_u32 src_w, td_u32 src_h, td_u32 src_stride_y, td_u32 src_stride_uv,
    td_bool src_is_nv21, unsigned char *dst_bgr)
{
    td_u32 y;
    td_u32 x;
    const unsigned char *src_y = src;
    const unsigned char *src_uv = src + (size_t)src_stride_y * src_h;
    for (y = 0; y < src_h; y++) {
        const unsigned char *y_row = src_y + (size_t)y * src_stride_y;
        const unsigned char *uv_row = src_uv + (size_t)(y / 2) * src_stride_uv;
        unsigned char *dst_row = dst_bgr + (size_t)y * src_w * 3;
        for (x = 0; x < src_w; x++) {
            int yy = (int)y_row[x];
            int u;
            int v;
            int c;
            int d;
            int e;
            int r;
            int g;
            int b;
            if (src_is_nv21 == TD_TRUE) {
                v = (int)uv_row[x & ~1U];
                u = (int)uv_row[(x & ~1U) + 1U];
            } else {
                u = (int)uv_row[x & ~1U];
                v = (int)uv_row[(x & ~1U) + 1U];
            }
            /* OpenCV-compatible BT.601 limited-range path. */
            c = yy - 16;
            if (c < 0) {
                c = 0;
            }
            d = u - 128;
            e = v - 128;
            r = (298 * c + 409 * e + 128) >> 8;
            g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            b = (298 * c + 516 * d + 128) >> 8;
            dst_row[x * 3 + 0] = yolo_u8_clip(b);
            dst_row[x * 3 + 1] = yolo_u8_clip(g);
            dst_row[x * 3 + 2] = yolo_u8_clip(r);
        }
    }
}

static void yolo_letterbox_bgr_bilinear(const unsigned char *src_bgr, td_u32 src_w, td_u32 src_h,
    unsigned char *dst_bgr, td_u32 dst_w, td_u32 dst_h, unsigned char pad)
{
    td_u32 scaled_w;
    td_u32 scaled_h;
    td_u32 off_x;
    td_u32 off_y;
    td_u32 y;
    td_u32 x;
    float r;

    /* Match Ultralytics LetterBox: r=min(new/old), then rounded unpadded size. */
    r = ((float)dst_w / (float)src_w < (float)dst_h / (float)src_h) ?
        ((float)dst_w / (float)src_w) : ((float)dst_h / (float)src_h);
    scaled_w = (td_u32)(src_w * r + 0.5f);
    scaled_h = (td_u32)(src_h * r + 0.5f);
    if (scaled_w < 2) scaled_w = 2;
    if (scaled_h < 2) scaled_h = 2;
    if (scaled_w > dst_w) scaled_w = dst_w;
    if (scaled_h > dst_h) scaled_h = dst_h;

    off_x = (dst_w - scaled_w) / 2;
    off_y = (dst_h - scaled_h) / 2;

    (void)memset(dst_bgr, pad, (size_t)dst_w * dst_h * 3);

    for (y = 0; y < scaled_h; y++) {
        /* OpenCV INTER_LINEAR-like half-pixel mapping. */
        float syf = ((float)y + 0.5f) * (float)src_h / (float)scaled_h - 0.5f;
        int sy0 = (int)floorf(syf);
        float fy = syf - (float)sy0;
        int sy1 = sy0 + 1;
        td_u32 c;
        if (sy0 < 0) {
            sy0 = 0;
            fy = 0.0f;
        }
        if (sy1 >= (int)src_h) {
            sy1 = (int)src_h - 1;
        }
        const unsigned char *row0 = src_bgr + (size_t)sy0 * src_w * 3;
        const unsigned char *row1 = src_bgr + (size_t)sy1 * src_w * 3;
        unsigned char *drow = dst_bgr + ((size_t)(off_y + y) * dst_w + off_x) * 3;
        for (x = 0; x < scaled_w; x++) {
            float sxf = ((float)x + 0.5f) * (float)src_w / (float)scaled_w - 0.5f;
            int sx0 = (int)floorf(sxf);
            float fx = sxf - (float)sx0;
            int sx1 = sx0 + 1;
            if (sx0 < 0) {
                sx0 = 0;
                fx = 0.0f;
            }
            if (sx1 >= (int)src_w) {
                sx1 = (int)src_w - 1;
            }
            for (c = 0; c < 3; c++) {
                float p00 = (float)row0[sx0 * 3 + c];
                float p01 = (float)row0[sx1 * 3 + c];
                float p10 = (float)row1[sx0 * 3 + c];
                float p11 = (float)row1[sx1 * 3 + c];
                float v0 = p00 + (p01 - p00) * fx;
                float v1 = p10 + (p11 - p10) * fx;
                int vi = (int)(v0 + (v1 - v0) * fy + 0.5f);
                drow[x * 3 + c] = yolo_u8_clip(vi);
            }
        }
    }
}

static td_bool yolo_bgr_to_nv12_u8(const unsigned char *bgr, td_u32 w, td_u32 h, unsigned char *dst_nv12)
{
    td_u32 y;
    td_u32 x;
    size_t y_size;
    unsigned char *dst_y;
    unsigned char *dst_uv;

    if ((bgr == TD_NULL) || (dst_nv12 == TD_NULL) || ((w & 1U) != 0) || ((h & 1U) != 0)) {
        return TD_FALSE;
    }
    y_size = (size_t)w * h;
    dst_y = dst_nv12;
    dst_uv = dst_nv12 + y_size;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const unsigned char *p = bgr + ((size_t)y * w + x) * 3;
            int b = (int)p[0];
            int g = (int)p[1];
            int r = (int)p[2];
            int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            dst_y[(size_t)y * w + x] = yolo_u8_clip(yy);
        }
    }

    for (y = 0; y < h; y += 2) {
        for (x = 0; x < w; x += 2) {
            int u_sum = 0;
            int v_sum = 0;
            td_u32 dy;
            td_u32 dx;
            for (dy = 0; dy < 2; dy++) {
                for (dx = 0; dx < 2; dx++) {
                    const unsigned char *p = bgr + ((size_t)(y + dy) * w + (x + dx)) * 3;
                    int b = (int)p[0];
                    int g = (int)p[1];
                    int r = (int)p[2];
                    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                    u_sum += u;
                    v_sum += v;
                }
            }
            dst_uv[((size_t)(y / 2) * w) + x] = yolo_u8_clip((u_sum + 2) / 4);
            dst_uv[((size_t)(y / 2) * w) + x + 1] = yolo_u8_clip((v_sum + 2) / 4);
        }
    }
    return TD_TRUE;
}

static void yolo_yuv420sp_add_bias(unsigned char *yuv, td_u32 w, td_u32 h, int y_add, int uv_add)
{
    size_t y_sz;
    size_t uv_sz;
    size_t i;
    unsigned char *y;
    unsigned char *uv;
    if ((yuv == TD_NULL) || (w == 0) || (h == 0)) {
        return;
    }
    if ((y_add == 0) && (uv_add == 0)) {
        return;
    }
    y_sz = (size_t)w * h;
    uv_sz = y_sz / 2;
    y = yuv;
    uv = yuv + y_sz;
    if (y_add != 0) {
        for (i = 0; i < y_sz; i++) {
            y[i] = yolo_u8_clip((int)y[i] + y_add);
        }
    }
    if (uv_add != 0) {
        for (i = 0; i < uv_sz; i++) {
            uv[i] = yolo_u8_clip((int)uv[i] + uv_add);
        }
    }
}

static void yolo_yuv420sp_affine(unsigned char *yuv, td_u32 w, td_u32 h,
    int y_mul_q8, int y_add, int uv_mul_q8, int uv_add)
{
    size_t y_sz;
    size_t uv_sz;
    size_t i;
    unsigned char *y;
    unsigned char *uv;
    if ((yuv == TD_NULL) || (w == 0) || (h == 0)) {
        return;
    }
    y_sz = (size_t)w * h;
    uv_sz = y_sz / 2;
    y = yuv;
    uv = yuv + y_sz;
    if (y_mul_q8 <= 0) y_mul_q8 = 256;
    if (uv_mul_q8 <= 0) uv_mul_q8 = 256;
    if ((y_mul_q8 == 256) && (y_add == 0) && (uv_mul_q8 == 256) && (uv_add == 0)) {
        return;
    }
    for (i = 0; i < y_sz; i++) {
        int v = ((int)y[i] * y_mul_q8 + 128) >> 8;
        v += y_add;
        y[i] = yolo_u8_clip(v);
    }
    for (i = 0; i < uv_sz; i++) {
        int v = ((int)uv[i] * uv_mul_q8 + 128) >> 8;
        v += uv_add;
        uv[i] = yolo_u8_clip(v);
    }
}

static td_bool prepare_model_input_rgb_fp32_from_nv12(const ot_video_frame_info *frame_info, const void *frame_ptr,
    void **input_host, size_t *input_size, td_u32 *net_w, td_u32 *net_h)
{
    td_u32 model_w = 640;
    td_u32 model_h = 640;
    td_u32 src_w;
    td_u32 src_h;
    td_u32 src_stride_y;
    td_u32 src_stride_uv;
    size_t chw = 0;
    unsigned char *src_bgr = TD_NULL;
    unsigned char *lb_bgr = TD_NULL;
    float *buf = TD_NULL;
    td_u32 x;
    td_u32 y;
    td_bool src_is_nv21 = TD_FALSE;
    static int s_uv_mode_fixed = -1; /* -1: undecided, 0: NV12(UV), 1: NV21(VU) */

    if ((frame_info == TD_NULL) || (frame_ptr == TD_NULL) || (input_host == TD_NULL) || (input_size == TD_NULL)) {
        return TD_FALSE;
    }
    (td_void)yolo_get_model_input_wh(&model_w, &model_h);
    if (net_w != TD_NULL) {
        *net_w = model_w;
    }
    if (net_h != TD_NULL) {
        *net_h = model_h;
    }

    src_w = frame_info->video_frame.width;
    src_h = frame_info->video_frame.height;
    src_stride_y = frame_info->video_frame.stride[0];
    src_stride_uv = frame_info->video_frame.stride[1];
    src_bgr = (unsigned char *)malloc((size_t)src_w * src_h * 3);
    lb_bgr = (unsigned char *)malloc((size_t)model_w * model_h * 3);
    if ((src_bgr == TD_NULL) || (lb_bgr == TD_NULL)) {
        free(src_bgr);
        free(lb_bgr);
        return TD_FALSE;
    }
    src_is_nv21 = vio_ai_pixel_format_is_nv21((td_u32)frame_info->video_frame.pixel_format);
    if (s_uv_mode_fixed < 0) {
        float target_r = 0.3289f;
        float target_g = 0.4374f;
        float target_b = 0.4353f;
        float best_err = 1e9f;
        int best_mode = src_is_nv21 ? 1 : 0;
        int mode;
        for (mode = 0; mode <= 1; mode++) {
            float rm;
            float gm;
            float bm;
            float err;
            yolo_yuv420sp_to_bgr_u8((const unsigned char *)frame_ptr, src_w, src_h, src_stride_y, src_stride_uv, (mode == 1) ? TD_TRUE : TD_FALSE, src_bgr);
            yolo_letterbox_bgr_bilinear(src_bgr, src_w, src_h, lb_bgr, model_w, model_h, 114U);
            yolo_bgr_mean_u8(lb_bgr, model_w, model_h, &rm, &gm, &bm);
            err = fabsf(rm - target_r) + fabsf(gm - target_g) + fabsf(bm - target_b);
            if (err < best_err) {
                best_err = err;
                best_mode = mode;
            }
        }
        s_uv_mode_fixed = best_mode;
        printf("yolo uv mode auto-select: %s (err=%.4f)\n", (s_uv_mode_fixed == 1) ? "NV21(VU)" : "NV12(UV)", best_err);
    }
    src_is_nv21 = (s_uv_mode_fixed == 1) ? TD_TRUE : TD_FALSE;
    /* Strict aligned pipeline: NV12/NV21 -> BGR -> letterbox(640,pad=114) -> RGB -> CHW -> /255. */
    yolo_yuv420sp_to_bgr_u8((const unsigned char *)frame_ptr, src_w, src_h, src_stride_y, src_stride_uv, src_is_nv21, src_bgr);
    yolo_letterbox_bgr_bilinear(src_bgr, src_w, src_h, lb_bgr, model_w, model_h, 114U);

    chw = (size_t)model_w * model_h;
    buf = (float *)malloc(chw * 3 * sizeof(float));
    if (buf == TD_NULL) {
        free(src_bgr);
        free(lb_bgr);
        return TD_FALSE;
    }

    for (y = 0; y < model_h; y++) {
        const unsigned char *bgr_row = lb_bgr + (size_t)y * model_w * 3;
        for (x = 0; x < model_w; x++) {
            size_t idx = (size_t)y * model_w + x;
            unsigned char b = bgr_row[x * 3 + 0];
            unsigned char g = bgr_row[x * 3 + 1];
            unsigned char r = bgr_row[x * 3 + 2];
            buf[idx] = (float)r * (1.0f / 255.0f);
            buf[idx + chw] = (float)g * (1.0f / 255.0f);
            buf[idx + chw * 2] = (float)b * (1.0f / 255.0f);
        }
    }

    free(src_bgr);
    free(lb_bgr);
    {
        static td_u32 s_rgb_dump_idx = 0;
        s_rgb_dump_idx++;
        if ((s_rgb_dump_idx % 60) == 1) {
            yolo_dump_rgb_chw_stats(buf, model_w, model_h);
        }
    }
    {
        float r_w = (float)model_w / (float)frame_info->video_frame.width;
        float r_h = (float)model_h / (float)frame_info->video_frame.height;
        float s = (r_w < r_h) ? r_w : r_h;
        float new_w = (float)frame_info->video_frame.width * s;
        float new_h = (float)frame_info->video_frame.height * s;
        g_preproc_meta.scale = s;
        g_preproc_meta.pad_x = ((float)model_w - new_w) * 0.5f;
        g_preproc_meta.pad_y = ((float)model_h - new_h) * 0.5f;
        g_preproc_meta.net_w = model_w;
        g_preproc_meta.net_h = model_h;
        g_preproc_meta.img_w = frame_info->video_frame.width;
        g_preproc_meta.img_h = frame_info->video_frame.height;
    }
    *input_host = buf;
    *input_size = chw * 3 * sizeof(float);
    return TD_TRUE;
}

static td_bool prepare_model_input_from_nv12(const ot_video_frame_info *frame_info, const void *frame_ptr,
    void **input_host, size_t *input_size, td_u32 *net_w, td_u32 *net_h, size_t expect_in_size, aclDataType in_dtype)
{
    td_u32 model_w = 640;
    td_u32 model_h = 640;
    size_t need_size;
    void *buf;

    if ((frame_info == TD_NULL) || (frame_ptr == TD_NULL) || (input_host == TD_NULL) || (input_size == TD_NULL)) {
        return TD_FALSE;
    }
    (td_void)yolo_get_model_input_wh(&model_w, &model_h);
    need_size = (size_t)model_w * model_h * 3 / 2;
    if (net_w != TD_NULL) {
        *net_w = model_w;
    }
    if (net_h != TD_NULL) {
        *net_h = model_h;
    }

    yolo_ab_init_once();

    if ((in_dtype == ACL_FLOAT) && (expect_in_size == (size_t)model_w * model_h * 3 * sizeof(float))) {
        static td_bool s_rgb_path_printed = TD_FALSE;
        if (s_rgb_path_printed == TD_FALSE) {
            printf("yolo input path: software YUV->RGB NCHW FP32 (/255)\n");
            s_rgb_path_printed = TD_TRUE;
        }
        return prepare_model_input_rgb_fp32_from_nv12(frame_info, frame_ptr, input_host, input_size, net_w, net_h);
    }

    buf = malloc(need_size);
    if (buf == TD_NULL) {
        return TD_FALSE;
    }

    {
        td_bool src_is_nv21 = vio_ai_pixel_format_is_nv21((td_u32)frame_info->video_frame.pixel_format);
        td_bool dst_is_nv21 = (g_ab_feed_nv12 != 0) ? TD_FALSE : TD_TRUE;
        if ((frame_info->video_frame.width == model_w) && (frame_info->video_frame.height == model_h)) {
            /* VPSS already gives 640x640: keep pixels untouched, only unify UV order if needed. */
            const unsigned char *src = (const unsigned char *)frame_ptr;
            unsigned char *dst = (unsigned char *)buf;
            td_u32 y;
            td_u32 x;
            td_u32 src_stride_y = frame_info->video_frame.stride[0];
            td_u32 src_stride_uv = frame_info->video_frame.stride[1];
            td_u32 uv_off = model_w * model_h;
            for (y = 0; y < model_h; y++) {
                (td_void)memcpy_s(dst + (size_t)y * model_w, model_w,
                    src + (size_t)y * src_stride_y, model_w);
            }
            if (src_is_nv21 == dst_is_nv21) {
                for (y = 0; y < model_h / 2; y++) {
                    (td_void)memcpy_s(dst + uv_off + (size_t)y * model_w, model_w,
                        src + (size_t)src_stride_y * model_h + (size_t)y * src_stride_uv, model_w);
                }
            } else {
                for (y = 0; y < model_h / 2; y++) {
                    const unsigned char *src_uv = src + (size_t)src_stride_y * model_h + (size_t)y * src_stride_uv;
                    unsigned char *dst_uv = dst + uv_off + (size_t)y * model_w;
                    for (x = 0; x + 1 < model_w; x += 2) {
                        dst_uv[x] = src_uv[x + 1];
                        dst_uv[x + 1] = src_uv[x];
                    }
                }
            }
        } else if (g_ab_letterbox != 0) {
#if YOLO_DUMP_SRC_CROP_640
            static td_bool s_src_crop_dumped = TD_FALSE;
            if (s_src_crop_dumped == TD_FALSE) {
                const td_u32 crop_w = model_w;
                const td_u32 crop_h = model_h;
                const td_u32 src_w = (td_u32)frame_info->video_frame.width;
                const td_u32 src_h = (td_u32)frame_info->video_frame.height;
                const td_u32 src_stride_y = (td_u32)frame_info->video_frame.stride[0];
                const td_u32 src_stride_uv = (td_u32)frame_info->video_frame.stride[1];

                td_u32 start_x = 0;
                td_u32 start_y = 0;
                if ((src_w > crop_w) && (src_h > crop_h)) {
                    start_x = (src_w - crop_w) / 2;
                    start_y = (src_h - crop_h) / 2;
                    start_x &= ~1U;
                    start_y &= ~1U;
                }

                size_t crop_y_sz = (size_t)crop_w * crop_h;
                size_t crop_uv_sz = crop_y_sz / 2;
                unsigned char *crop = (unsigned char *)malloc(crop_y_sz + crop_uv_sz);
                if (crop != TD_NULL) {
                    unsigned char *crop_y = crop;
                    unsigned char *crop_uv = crop + crop_y_sz;
                    const unsigned char *src_y = (const unsigned char *)frame_ptr;
                    const unsigned char *src_uv = src_y + (size_t)src_stride_y * src_h;

                    td_u32 yy;
                    for (yy = 0; yy < crop_h; yy++) {
                        const unsigned char *srow = src_y + (size_t)(start_y + yy) * src_stride_y + start_x;
                        unsigned char *drow = crop_y + (size_t)yy * crop_w;
                        (void)memcpy(drow, srow, crop_w);
                    }
                    for (yy = 0; yy < crop_h / 2; yy++) {
                        const unsigned char *srow = src_uv + (size_t)(start_y / 2 + yy) * src_stride_uv + start_x;
                        unsigned char *drow = crop_uv + (size_t)yy * crop_w;
                        (void)memcpy(drow, srow, crop_w);
                    }

                    const char *crop_path = (src_is_nv21 != TD_FALSE) ?
                        "/tmp/aipp_src_crop_640x640_nv21.yuv" :
                        "/tmp/aipp_src_crop_640x640_nv12.yuv";
                    FILE *cf = fopen(crop_path, "wb");
                    if (cf != TD_NULL) {
                        (void)fwrite(crop, 1, crop_y_sz + crop_uv_sz, cf);
                        (void)fclose(cf);
                        printf("yolo src crop dump: wrote %s\n", crop_path);
                    }
                    free(crop);
                    s_src_crop_dumped = TD_TRUE;
                }
            }
#endif
            if (vio_ai_resize_yuv420sp_letterbox((const unsigned char *)frame_ptr,
                frame_info->video_frame.width,
                frame_info->video_frame.height,
                frame_info->video_frame.stride[0],
                frame_info->video_frame.stride[1],
                (unsigned char *)buf,
                model_w,
                model_h,
                src_is_nv21,
                dst_is_nv21) != TD_TRUE) {
                free(buf);
                return TD_FALSE;
            }
        } else {
            if (g_ab_bilinear != 0) {
                vio_ai_resize_yuv420sp_bilinear((const unsigned char *)frame_ptr,
                    frame_info->video_frame.width,
                    frame_info->video_frame.height,
                    frame_info->video_frame.stride[0],
                    frame_info->video_frame.stride[1],
                    (unsigned char *)buf,
                    model_w,
                    model_h,
                    src_is_nv21,
                    dst_is_nv21);
            } else {
                resize_yuv420sp_nn((const unsigned char *)frame_ptr,
                    frame_info->video_frame.width,
                    frame_info->video_frame.height,
                    frame_info->video_frame.stride[0],
                    frame_info->video_frame.stride[1],
                    (unsigned char *)buf,
                    model_w,
                    model_h,
                    src_is_nv21,
                    dst_is_nv21);
            }
        }
    }

    /* Before AIPP: optional affine to compensate range/CSC mismatch */
    yolo_yuv420sp_affine((unsigned char *)buf, model_w, model_h,
        g_ab_y_mul_q8, g_ab_y_add, g_ab_uv_mul_q8, g_ab_uv_add);
    yolo_try_dump_nv21_once((const unsigned char *)buf, model_w, model_h);
    if ((frame_info->video_frame.width == model_w) && (frame_info->video_frame.height == model_h)) {
        g_preproc_meta.scale = 1.0f;
        g_preproc_meta.pad_x = 0.0f;
        g_preproc_meta.pad_y = 0.0f;
    } else if (g_ab_letterbox != 0) {
        float r_w = (float)model_w / (float)frame_info->video_frame.width;
        float r_h = (float)model_h / (float)frame_info->video_frame.height;
        float s = (r_w < r_h) ? r_w : r_h;
        float new_w = roundf((float)frame_info->video_frame.width * s);
        float new_h = roundf((float)frame_info->video_frame.height * s);
        g_preproc_meta.scale = s;
        g_preproc_meta.pad_x = ((float)model_w - new_w) * 0.5f;
        g_preproc_meta.pad_y = ((float)model_h - new_h) * 0.5f;
    } else {
        g_preproc_meta.scale = 1.0f;
        g_preproc_meta.pad_x = 0.0f;
        g_preproc_meta.pad_y = 0.0f;
    }
    g_preproc_meta.net_w = model_w;
    g_preproc_meta.net_h = model_h;
    g_preproc_meta.img_w = frame_info->video_frame.width;
    g_preproc_meta.img_h = frame_info->video_frame.height;

#if YOLO_DEBUG_DIAG
    {
        static td_u32 s_dump_idx = 0;
        s_dump_idx++;
        if ((s_dump_idx % 60) == 1) {
            yolo_dump_nv21_stats((const unsigned char *)buf, model_w, model_h);
        }
    }
#endif

    *input_host = buf;
    *input_size = need_size;
    return TD_TRUE;
}

static td_bool yolo_pick_layout_by_dims(int out_idx, int *feat_num, int *anchor_num, td_bool *feat_first)
{
    aclmdlIODims dims;
    td_s32 ret;
    td_u32 d0;
    td_u32 d1;

    (td_void)memset_s(&dims, sizeof(dims), 0, sizeof(dims));
    ret = aclmdlGetOutputDims(g_model_desc, out_idx, &dims);
    if ((ret != ACL_SUCCESS) || (dims.dimCount < 2)) {
        return TD_FALSE;
    }

    d0 = dims.dims[dims.dimCount - 2];
    d1 = dims.dims[dims.dimCount - 1];
    if ((d0 >= 5) && (d0 <= 256) && (d1 >= 100)) {
        *feat_num = (int)d0;
        *anchor_num = (int)d1;
        *feat_first = TD_TRUE;
        return TD_TRUE;
    }
    if ((d1 >= 5) && (d1 <= 256) && (d0 >= 100)) {
        *feat_num = (int)d1;
        *anchor_num = (int)d0;
        *feat_first = TD_FALSE;
        return TD_TRUE;
    }
    return TD_FALSE;
}

static int yolo_decode(const float *out_data, size_t out_float_num, int net_w, int net_h, int img_w, int img_h,
    yolo_det_t *raw_dets, int max_raw, int out_idx)
{
    int feat_num = 0;
    int anchor_num = 0;
    int a;
    int det_cnt = 0;
    yolo_det_t tmp_raw[YOLO_MAX_DET];
    yolo_det_t tmp_nms[YOLO_MAX_DET];
    td_bool pref_feat_first = TD_TRUE;
    td_bool have_pref = TD_FALSE;
    td_bool feat_first = TD_TRUE;
    td_bool has_obj = TD_FALSE;
    int cls_start;
    int cls_end;
    float sx;
    float sy;
    float conf_thres = g_decode_conf_thres;
    int local_cnt = 0;
    float best_metric = -1e9f;
    td_bool best_xyxy = TD_FALSE;
    int best_cnt = 0;

    if (yolo_pick_layout_by_dims(out_idx, &feat_num, &anchor_num, &pref_feat_first) == TD_TRUE) {
        have_pref = TD_TRUE;
    } else {
        if (yolo_pick_layout(out_data, out_float_num, &feat_num, &anchor_num, &feat_first) != TD_TRUE) {
            printf("yolo decode: unsupported layout out_float_num=%u\n", (td_u32)out_float_num);
            return 0;
        }
    }
    if ((size_t)feat_num * (size_t)anchor_num > out_float_num) {
        printf("yolo decode: unsupported output float_num=%u\n", (td_u32)out_float_num);
        return 0;
    }
    if (feat_num <= 5) {
        return 0;
    }

    if (have_pref == TD_TRUE) {
        feat_first = pref_feat_first;
    }
    has_obj = TD_FALSE;
    cls_start = 4;
    {
        int num_cls = YOLO_NUM_CLASSES;
        if (g_infer_post_mode == YOLO_INFER_POST_PERSON) {
            num_cls = 80;
        }
        cls_end = cls_start + num_cls;
    }
    if (cls_end > feat_num) {
        cls_end = feat_num;
    }
    sx = (float)img_w / (float)net_w;
    sy = (float)img_h / (float)net_h;
#if YOLO_DEBUG_DIAG
    conf_thres = YOLO_DEBUG_CONF_THRES;
#endif

    {
        int cnt = 0;
        float sum_score = 0.0f;
        float edge_penalty = 0.0f;
        float huge_penalty = 0.0f;

        for (a = 0; a < anchor_num; a++) {
            float cx;
            float cy;
            float w;
            float h;
            float best_score = -FLT_MAX;
            int best_cls = -1;
            int c;
            float obj_score = 1.0f;
            float x1;
            float y1;
            float x2;
            float y2;
            const td_bool ff = feat_first;

            cx = ff ? out_data[0 * anchor_num + a] : out_data[a * feat_num + 0];
            cy = ff ? out_data[1 * anchor_num + a] : out_data[a * feat_num + 1];
            w = ff ? out_data[2 * anchor_num + a] : out_data[a * feat_num + 2];
            h = ff ? out_data[3 * anchor_num + a] : out_data[a * feat_num + 3];
            if (g_infer_post_mode == YOLO_INFER_POST_PERSON &&
                cx <= 2.0f && cy <= 2.0f && w <= 2.0f && h <= 2.0f) {
                cx *= (float)net_w;
                cy *= (float)net_h;
                w *= (float)net_w;
                h *= (float)net_h;
            }
            for (c = cls_start; c < cls_end; c++) {
                float s = ff ? yolo_prob(out_data[c * anchor_num + a]) : yolo_prob(out_data[a * feat_num + c]);
                if (s > best_score) {
                    best_score = s;
                    best_cls = c - cls_start;
                }
            }
            best_score *= obj_score;
            if (best_score < conf_thres) {
                continue;
            }

            /* deploy_pack_v2 output semantics: xywh are absolute pixels in 640x640 space. */

            /* Keep coords in network space first; map to image later once. */
            x1 = (cx - w * 0.5f);
            y1 = (cy - h * 0.5f);
            x2 = (cx + w * 0.5f);
            y2 = (cy + h * 0.5f);
            if ((x2 <= x1) || (y2 <= y1)) {
                continue;
            }
            if (cnt >= YOLO_MAX_DET) {
                break;
            }
            {
                float px1;
                float py1;
                float px2;
                float py2;
                float bw;
                float bh;
                float area_ratio;
                float ar;
                td_bool use_letterbox_inv = TD_FALSE;
                if ((g_preproc_meta.net_w == (td_u32)net_w) &&
                    (g_preproc_meta.net_h == (td_u32)net_h) &&
                    (g_preproc_meta.scale > 1e-6f) &&
                    ((g_preproc_meta.pad_x > 0.5f) || (g_preproc_meta.pad_y > 0.5f) ||
                     (g_preproc_meta.scale < 0.999f) || (g_preproc_meta.scale > 1.001f))) {
                    use_letterbox_inv = TD_TRUE;
                }
                if (use_letterbox_inv == TD_TRUE) {
                    /* letterbox path: inverse by pad/scale only (no extra sx/sy) */
                    px1 = (x1 - g_preproc_meta.pad_x) / g_preproc_meta.scale;
                    py1 = (y1 - g_preproc_meta.pad_y) / g_preproc_meta.scale;
                    px2 = (x2 - g_preproc_meta.pad_x) / g_preproc_meta.scale;
                    py2 = (y2 - g_preproc_meta.pad_y) / g_preproc_meta.scale;
                } else {
                    /* plain resize path: scale net-space to image-space */
                    px1 = x1 * sx;
                    py1 = y1 * sy;
                    px2 = x2 * sx;
                    py2 = y2 * sy;
                }
                tmp_raw[cnt].x1 = yolo_clamp(px1, 0.0f, (float)(img_w - 1));
                tmp_raw[cnt].y1 = yolo_clamp(py1, 0.0f, (float)(img_h - 1));
                tmp_raw[cnt].x2 = yolo_clamp(px2, 0.0f, (float)(img_w - 1));
                tmp_raw[cnt].y2 = yolo_clamp(py2, 0.0f, (float)(img_h - 1));
                bw = tmp_raw[cnt].x2 - tmp_raw[cnt].x1;
                bh = tmp_raw[cnt].y2 - tmp_raw[cnt].y1;
                if ((bw < 2.0f) || (bh < 2.0f)) {
                    continue;
                }
                area_ratio = (bw * bh) / ((float)img_w * (float)img_h + 1e-6f);
                ar = bw / (bh + 1e-6f);
                /* Hard reject obvious garbage boxes (full-frame/edge-covering anomalies). */
                if (area_ratio > 0.70f) {
                    continue;
                }
                if ((((tmp_raw[cnt].x1 <= 1.0f) && (tmp_raw[cnt].x2 >= (float)(img_w - 2))) ||
                     ((tmp_raw[cnt].y1 <= 1.0f) && (tmp_raw[cnt].y2 >= (float)(img_h - 2)))) &&
                    (area_ratio > 0.45f)) {
                    continue;
                }
                if ((ar < 0.08f) || (ar > 12.0f)) {
                    continue;
                }
            }
            tmp_raw[cnt].score = best_score;
            tmp_raw[cnt].cls_id = best_cls;

            sum_score += best_score;
            {
                float bw = tmp_raw[cnt].x2 - tmp_raw[cnt].x1;
                float bh = tmp_raw[cnt].y2 - tmp_raw[cnt].y1;
                float area_ratio = (bw * bh) / ((float)img_w * (float)img_h + 1e-6f);
                if (area_ratio > 0.80f) {
                    huge_penalty += (area_ratio - 0.80f);
                }
                if ((tmp_raw[cnt].x1 <= 1.0f) || (tmp_raw[cnt].y1 <= 1.0f) ||
                    (tmp_raw[cnt].x2 >= (float)(img_w - 2)) || (tmp_raw[cnt].y2 >= (float)(img_h - 2))) {
                    edge_penalty += 1.0f;
                }
            }
            cnt++;
        }

        if (cnt > 0) {
            float metric = (sum_score / (float)cnt) - (edge_penalty / (float)cnt) * 0.05f - (huge_penalty / (float)cnt) * 0.8f;
            if (metric > best_metric) {
                int i;
                best_metric = metric;
                best_xyxy = TD_FALSE;
                best_cnt = cnt;
                for (i = 0; i < best_cnt; i++) {
                    raw_dets[i] = tmp_raw[i];
                }
            }
        }
    }

    if (best_cnt <= 0) {
        return 0;
    }
    if ((best_metric > -1e8f) && ((best_cnt > 0) && (best_cnt <= YOLO_MAX_DET))) {
        printf("yolo box layout pick: %s raw=%d metric=%.4f\n", (best_xyxy == TD_TRUE) ? "xyxy" : "xywh", best_cnt, best_metric);
    }
    det_cnt = yolo_nms(raw_dets, best_cnt, tmp_nms, YOLO_MAX_DET, g_decode_iou_thres);
    if (det_cnt > max_raw) {
        det_cnt = max_raw;
    }
    for (a = 0; a < det_cnt; a++) {
        raw_dets[a] = tmp_nms[a];
    }
    return det_cnt;
}



static void draw_dets_on_nv12(unsigned char *nv12, td_u32 stride, td_u32 img_w, td_u32 img_h)
{
    int i;
    int draw_n = (g_draw_det_cnt < 20) ? g_draw_det_cnt : 20;
    int thick = (img_w >= 1920) ? 6 : 3;
    if (nv12 == TD_NULL) {
        return;
    }
    for (i = 0; i < draw_n; i++) {
        int x1 = (int)g_draw_dets[i].x1;
        int y1 = (int)g_draw_dets[i].y1;
        int x2 = (int)g_draw_dets[i].x2;
        int y2 = (int)g_draw_dets[i].y2;
        vio_ai_draw_box_y_plane(nv12, stride, img_w, img_h, x1, y1, x2, y2, 250, thick);
    }
    if (g_draw_det_cnt > 0) {
        /* Strong visual marker: frame border when any detection exists. */
        vio_ai_draw_box_y_plane(nv12, stride, img_w, img_h, 0, 0, (int)img_w - 1, (int)img_h - 1, 250, thick);
    }
}

static void yolo_postprocess_and_dump(const float *out_data, size_t out_float_num, td_u32 img_w, td_u32 img_h, int out_idx)
{
    aclmdlIODims dims;
    td_u32 net_w = 640;
    td_u32 net_h = 640;
    yolo_det_t raw_dets[YOLO_MAX_DET];
    yolo_det_t nms_dets[YOLO_MAX_DET];
    int raw_cnt;
    int det_cnt;
    int i;
    int feat_num = 0;
    int anchor_num = 0;
    td_bool feat_first = TD_TRUE;
    float cls_max = -FLT_MAX;
    float cls_min = FLT_MAX;
    float box_abs_max = 0.0f;
    float out_min = FLT_MAX;
    float out_max = -FLT_MAX;
    double out_sum = 0.0;
    int top_cls = -1;
    float top_cls_score = 0.0f;
    td_bool has_obj_small = TD_FALSE;
    int cls_start_small = 4;
    int cls_end_small = 4;
    static td_u32 frame_idx = 0;
    td_s32 ret;

    frame_idx++;
    (td_void)memset_s(&dims, sizeof(dims), 0, sizeof(dims));
    ret = aclmdlGetInputDims(g_model_desc, 0, &dims);
    if ((ret == ACL_SUCCESS) && (dims.dimCount >= 4)) {
        td_u32 a = dims.dims[dims.dimCount - 2];
        td_u32 b = dims.dims[dims.dimCount - 1];
        if ((a >= b) && (a >= 64)) {
            net_h = a;
            net_w = b;
        } else {
            net_h = b;
            net_w = a;
        }
        if ((net_w < 64) || (net_h < 64)) {
            net_w = 640;
            net_h = 640;
        }
    }

    raw_cnt = yolo_decode(out_data, out_float_num, (int)net_w, (int)net_h, (int)img_w, (int)img_h, raw_dets, YOLO_MAX_DET, out_idx);
    det_cnt = yolo_nms(raw_dets, raw_cnt, nms_dets, YOLO_MAX_DET, g_decode_iou_thres);

#if YOLO_DEBUG_DIAG
    {
        size_t k;
        size_t n = out_float_num;
        if (n > 200000) {
            n = 200000;
        }
        for (k = 0; k < n; k++) {
            float v = out_data[k];
            if (v < out_min) out_min = v;
            if (v > out_max) out_max = v;
            out_sum += (double)v;
        }
    }
#endif

    if (yolo_pick_layout_by_dims(out_idx, &feat_num, &anchor_num, &feat_first) == TD_TRUE) {
        int a;
        int c;
        for (a = 0; a < anchor_num; a++) {
            float x = feat_first ? out_data[0 * anchor_num + a] : out_data[a * feat_num + 0];
            float y = feat_first ? out_data[1 * anchor_num + a] : out_data[a * feat_num + 1];
            float w = feat_first ? out_data[2 * anchor_num + a] : out_data[a * feat_num + 2];
            float h = feat_first ? out_data[3 * anchor_num + a] : out_data[a * feat_num + 3];
            float abs_max = fabsf(x);
            if (fabsf(y) > abs_max) abs_max = fabsf(y);
            if (fabsf(w) > abs_max) abs_max = fabsf(w);
            if (fabsf(h) > abs_max) abs_max = fabsf(h);
            if (abs_max > box_abs_max) box_abs_max = abs_max;
            for (c = 4; c < feat_num; c++) {
                float v = feat_first ? out_data[c * anchor_num + a] : out_data[a * feat_num + c];
                if (v > cls_max) cls_max = v;
                if (v < cls_min) cls_min = v;
            }
        }
    }
    if (yolo_pick_layout_by_dims(out_idx, &feat_num, &anchor_num, &feat_first) == TD_TRUE) {
        int a;
        int c;
        if (feat_num == (YOLO_NUM_CLASSES + 4)) {
            has_obj_small = TD_FALSE;
        } else if (feat_num == (YOLO_NUM_CLASSES + 5)) {
            has_obj_small = TD_TRUE;
        } else if (feat_num >= 85) {
            has_obj_small = TD_TRUE;
        } else {
            has_obj_small = TD_FALSE;
        }
        cls_start_small = has_obj_small ? 5 : 4;
        cls_end_small = cls_start_small + YOLO_NUM_CLASSES;
        if (cls_end_small > feat_num) {
            cls_end_small = feat_num;
        }
        for (a = 0; a < anchor_num; a++) {
            for (c = cls_start_small; c < cls_end_small; c++) {
                float v = feat_first ? out_data[c * anchor_num + a] : out_data[a * feat_num + c];
                float p = yolo_prob(v);
                if (p > top_cls_score) {
                    top_cls_score = p;
                    top_cls = c - cls_start_small;
                }
            }
        }
    }
    {
        float top_det_score = 0.0f;
        int t;
        for (t = 0; t < det_cnt; t++) {
            if (nms_dets[t].score > top_det_score) {
                top_det_score = nms_dets[t].score;
            }
        }
        g_last_top_p = (top_det_score > 0.0f) ? top_det_score : top_cls_score;
    }

    if ((det_cnt > 0) || ((frame_idx % 60) == 0)) {
        int show_n = (det_cnt < YOLO_PRINT_TOPK) ? det_cnt : YOLO_PRINT_TOPK;
        printf("yolo frame=%u det=%d raw=%d net=%ux%u img=%ux%u cls[min,max]=[%.4f,%.4f] box_abs_max=%.4f",
            frame_idx, det_cnt, raw_cnt, net_w, net_h, img_w, img_h, cls_min, cls_max, box_abs_max);
#if YOLO_DEBUG_DIAG
        if (out_float_num > 0) {
            double mean = out_sum / (double)((out_float_num > 200000) ? 200000 : out_float_num);
            printf(" out[min,max,mean]=[%.4f,%.4f,%.4f] n=%u",
                out_min, out_max, (float)mean, (td_u32)out_float_num);
        }
#endif
        if (top_cls >= 0) {
            printf(" top_cls=%d top_p=%.4f", top_cls, top_cls_score);
            if ((top_cls >= 0) && (top_cls < 6)) {
                printf("(%s)", g_cls_names6[top_cls]);
            }
            printf(" show_th=%.4f", yolo_dynamic_show_thres());
        }
        printf("\n");
#if YOLO_DEBUG_DIAG
        if ((feat_num > 0) && (feat_num <= 16) && (anchor_num > 0)) {
            int c;
            for (c = 0; c < feat_num; c++) {
                int a;
                float mn = FLT_MAX, mx = -FLT_MAX;
                double sm = 0.0;
                for (a = 0; a < anchor_num; a++) {
                    float v = feat_first ? out_data[c * anchor_num + a] : out_data[a * feat_num + c];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                    sm += (double)v;
                }
                printf("  ch[%d] raw[min,max,mean]=[%.4f,%.4f,%.4f]%s\n",
                    c, mn, mx, (float)(sm / (double)anchor_num),
                    (has_obj_small && c == 4) ? " (maybe obj)" : "");
            }
        }
#endif
        for (i = 0; i < show_n; i++) {
            printf("  box[%d] cls=%d score=%.3f xyxy=(%.1f, %.1f, %.1f, %.1f)\n",
                i, nms_dets[i].cls_id, nms_dets[i].score,
                nms_dets[i].x1, nms_dets[i].y1, nms_dets[i].x2, nms_dets[i].y2);
        }
    }
    {
        yolo_det_t picked[YOLO_TRACK_SLOTS];
        int pick_n;

        yolo_det_t motion_pick;
        int motion_n = 0;
        const yolo_det_t *motion_src = picked;
        int motion_cnt = 0;

        pick_n = yolo_select_for_display(nms_dets, det_cnt, picked, YOLO_TRACK_SLOTS, img_w, img_h);
        if ((det_cnt > 0) && (pick_n == 0)) {
            pick_n = 1;
            picked[0] = nms_dets[0];
        }
        motion_n = yolo_pick_motion_box(nms_dets, det_cnt, (float)img_w, (float)img_h, &motion_pick);
        if (motion_n > 0) {
            motion_src = &motion_pick;
            motion_cnt = 1;
        } else if (pick_n > 0) {
            motion_src = picked;
            motion_cnt = pick_n;
        }
        g_draw_det_cnt = yolo_track_update(picked, pick_n, g_draw_dets, YOLO_RGN_MAX);
        yolo_export_action_state(motion_src, motion_cnt, g_draw_dets, g_draw_det_cnt,
            nms_dets, det_cnt, img_w, img_h, frame_idx);
    }
    if ((det_cnt > 0) && (g_draw_det_cnt == 0)) {
        printf("yolo display filtered all: det=%d show=%d th=%.3f\n",
            det_cnt, g_draw_det_cnt, YOLO_SHOW_SCORE_THRES);
    }
}

/* COCO YOLOv8n：仅解码 person 类（feature index 4），坐标还原到预览分辨率 */
static int yolo_decode_coco_person(const float *out_data, size_t out_float_num, int net_w, int net_h,
    int img_w, int img_h, yolo_det_t *raw_dets, int max_raw)
{
    int feat_num = 0;
    int anchor_num = 0;
    td_bool feat_first = TD_TRUE;
    yolo_det_t tmp[YOLO_MAX_DET];
    int cnt = 0;
    int a;
    float conf_thres = vio_ai_env_get_float_default("WIDGET_YOLO_PERSON_CONF", 0.25f);
    const int person_feat = 4; /* YOLOv8: channel 4 = COCO class 0 (person) */

    if (conf_thres < 0.05f) {
        conf_thres = 0.05f;
    }

    if (yolo_pick_layout_by_dims(0, &feat_num, &anchor_num, &feat_first) != TD_TRUE) {
        if (yolo_pick_layout(out_data, out_float_num, &feat_num, &anchor_num, &feat_first) != TD_TRUE) {
            static td_bool s_layout_warn = TD_FALSE;
            if (s_layout_warn == TD_FALSE) {
                printf("yolo_decode_coco_person: layout pick failed out_float_num=%u\n", (td_u32)out_float_num);
                s_layout_warn = TD_TRUE;
            }
            return 0;
        }
    }
    if (feat_num <= person_feat || max_raw <= 0) {
        return 0;
    }
    if ((size_t)feat_num * (size_t)anchor_num > out_float_num) {
        return 0;
    }

    for (a = 0; a < anchor_num && cnt < YOLO_MAX_DET; a++) {
        float cx;
        float cy;
        float w;
        float h;
        float score;
        float x1;
        float y1;
        float x2;
        float y2;
        float px1;
        float py1;
        float px2;
        float py2;
        float bw;
        float bh;
        float area_ratio;
        const td_bool ff = feat_first;

        cx = ff ? out_data[0 * anchor_num + a] : out_data[a * feat_num + 0];
        cy = ff ? out_data[1 * anchor_num + a] : out_data[a * feat_num + 1];
        w = ff ? out_data[2 * anchor_num + a] : out_data[a * feat_num + 2];
        h = ff ? out_data[3 * anchor_num + a] : out_data[a * feat_num + 3];
        score = ff ? yolo_prob(out_data[person_feat * anchor_num + a]) :
            yolo_prob(out_data[a * feat_num + person_feat]);

        if (score < conf_thres) {
            continue;
        }

        if (cx <= 2.0f && cy <= 2.0f && w <= 2.0f && h <= 2.0f) {
            cx *= (float)net_w;
            cy *= (float)net_h;
            w *= (float)net_w;
            h *= (float)net_h;
        }

        x1 = cx - w * 0.5f;
        y1 = cy - h * 0.5f;
        x2 = cx + w * 0.5f;
        y2 = cy + h * 0.5f;
        if ((x2 <= x1) || (y2 <= y1)) {
            continue;
        }

        if ((g_preproc_meta.net_w == (td_u32)net_w) && (g_preproc_meta.net_h == (td_u32)net_h) &&
            (g_preproc_meta.scale > 1e-6f) &&
            ((g_preproc_meta.pad_x > 0.5f) || (g_preproc_meta.pad_y > 0.5f) ||
             (g_preproc_meta.scale < 0.999f) || (g_preproc_meta.scale > 1.001f))) {
            px1 = (x1 - g_preproc_meta.pad_x) / g_preproc_meta.scale;
            py1 = (y1 - g_preproc_meta.pad_y) / g_preproc_meta.scale;
            px2 = (x2 - g_preproc_meta.pad_x) / g_preproc_meta.scale;
            py2 = (y2 - g_preproc_meta.pad_y) / g_preproc_meta.scale;
        } else {
            float sx = (float)img_w / (float)net_w;
            float sy = (float)img_h / (float)net_h;
            px1 = x1 * sx;
            py1 = y1 * sy;
            px2 = x2 * sx;
            py2 = y2 * sy;
        }

        bw = px2 - px1;
        bh = py2 - py1;
        if ((bw < 8.0f) || (bh < 12.0f)) {
            continue;
        }
        area_ratio = (bw * bh) / ((float)img_w * (float)img_h + 1e-6f);
        if (area_ratio < 0.005f || area_ratio > 0.92f) {
            continue;
        }

        tmp[cnt].x1 = yolo_clamp(px1, 0.0f, (float)(img_w - 1));
        tmp[cnt].y1 = yolo_clamp(py1, 0.0f, (float)(img_h - 1));
        tmp[cnt].x2 = yolo_clamp(px2, 0.0f, (float)(img_w - 1));
        tmp[cnt].y2 = yolo_clamp(py2, 0.0f, (float)(img_h - 1));
        tmp[cnt].score = score;
        tmp[cnt].cls_id = YOLO_COCO_PERSON_CLASS_ID;
        cnt++;
    }

    return yolo_nms(tmp, cnt, raw_dets, max_raw, g_decode_iou_thres);
}

static void yolo_scale_dets_to_preview(yolo_det_t *dets, int n, td_u32 src_w, td_u32 src_h)
{
    float sx;
    float sy;
    int i;

    if ((dets == TD_NULL) || (n <= 0) || (src_w == 0) || (src_h == 0)) {
        return;
    }
    yolo_refresh_preview_src_size();
    sx = (float)g_preview_src_w / (float)src_w;
    sy = (float)g_preview_src_h / (float)src_h;
    for (i = 0; i < n; i++) {
        dets[i].x1 *= sx;
        dets[i].x2 *= sx;
        dets[i].y1 *= sy;
        dets[i].y2 *= sy;
    }
}

static void yolo_postprocess_person_draw_only(const float *out_data, size_t out_float_num, td_u32 img_w, td_u32 img_h)
{
    aclmdlIODims dims;
    aclmdlIODims odims;
    td_u32 net_w = 640;
    td_u32 net_h = 640;
    td_u32 det_w;
    td_u32 det_h;
    td_u32 disp_w;
    td_u32 disp_h;
    yolo_det_t nms_dets[YOLO_MAX_DET];
    yolo_det_t picked[1];
    int det_cnt;
    int pick_n;
    int best_i = -1;
    int i;
    td_s32 ret;
    int saved_post;
    int saved_target;
    float saved_conf;
    float person_smooth = vio_ai_env_get_float_default("WIDGET_YOLO_PERSON_SMOOTH", 0.55f);
    float person_conf = vio_ai_env_get_float_default("WIDGET_YOLO_PERSON_CONF", 0.25f);
    int det_feat = 0;

    (td_void)memset_s(&dims, sizeof(dims), 0, sizeof(dims));
    (td_void)memset_s(&odims, sizeof(odims), 0, sizeof(odims));
    ret = aclmdlGetInputDims(g_model_desc, 0, &dims);
    if ((ret == ACL_SUCCESS) && (dims.dimCount >= 4)) {
        td_u32 a = dims.dims[dims.dimCount - 2];
        td_u32 b = dims.dims[dims.dimCount - 1];
        if ((a >= b) && (a >= 64)) {
            net_h = a;
            net_w = b;
        } else {
            net_h = b;
            net_w = a;
        }
    }
    if (aclmdlGetOutputDims(g_model_desc, 0, &odims) == ACL_SUCCESS && odims.dimCount >= 2) {
        td_u32 d0 = odims.dims[odims.dimCount - 2];
        td_u32 d1 = odims.dims[odims.dimCount - 1];
        det_feat = (int)((d0 >= 5 && d0 <= 256) ? d0 : d1);
    }

    det_w = (img_w > 0) ? img_w : net_w;
    det_h = (img_h > 0) ? img_h : net_h;
    yolo_refresh_preview_src_size();
    disp_w = g_preview_src_w;
    disp_h = g_preview_src_h;
    yolo_tune_init_once();

    saved_post = g_infer_post_mode;
    saved_target = g_yolo_target_mode;
    saved_conf = g_yolo_action_conf_thres;
    if (person_conf < 0.05f) {
        person_conf = 0.05f;
    }
    g_yolo_action_conf_thres = person_conf;

    if (det_feat >= 84) {
        g_infer_post_mode = YOLO_INFER_POST_PERSON;
        det_cnt = yolo_decode_coco_person(out_data, out_float_num, (int)net_w, (int)net_h,
            (int)det_w, (int)det_h, nms_dets, YOLO_MAX_DET);
        for (i = 0; i < det_cnt; i++) {
            if (best_i < 0 || nms_dets[i].score > nms_dets[best_i].score) {
                best_i = i;
            }
        }
        pick_n = (best_i >= 0) ? 1 : 0;
        if (pick_n > 0) {
            picked[0] = nms_dets[best_i];
        }
    } else {
        /* 板端 /opt/yolov8n.om 实际为 5 类动作检测 (1x10x8400)，按人体大框优选 */
        g_infer_post_mode = YOLO_INFER_POST_AUTO;
        g_yolo_target_mode = YOLO_TARGET_MODE_PERSON;
        det_cnt = yolo_decode(out_data, out_float_num, (int)net_w, (int)net_h,
            (int)det_w, (int)det_h, nms_dets, YOLO_MAX_DET, 0);
        pick_n = yolo_select_for_display(nms_dets, det_cnt, picked, 1, det_w, det_h);
        if (pick_n <= 0 && det_cnt > 0) {
            float best_rank = -1.0f;
            for (i = 0; i < det_cnt; i++) {
                float rank = yolo_target_display_rank(&nms_dets[i], (float)det_w, (float)det_h);
                if (rank > best_rank) {
                    best_rank = rank;
                    best_i = i;
                }
            }
            if (best_i >= 0) {
                picked[0] = nms_dets[best_i];
                pick_n = 1;
            }
        }
    }

    g_infer_post_mode = saved_post;
    g_yolo_target_mode = saved_target;
    g_yolo_action_conf_thres = saved_conf;

    g_draw_det_cnt = 0;
    if (pick_n > 0) {
        yolo_det_t meas = picked[0];
        if (g_prev_show_cnt > 0 && person_smooth > 0.01f) {
            yolo_blend_box_with_alpha(&g_draw_dets[0], &meas, &g_prev_show_dets[0],
                person_smooth, person_smooth * 0.85f);
            g_draw_dets[0].score = meas.score;
        } else {
            g_draw_dets[0] = meas;
        }
        g_draw_det_cnt = 1;
        g_prev_show_dets[0] = g_draw_dets[0];
        g_prev_show_cnt = 1;
    } else {
        g_prev_show_cnt = 0;
    }

    if (g_yolo_box_draw != 0) {
        yolo_rgn_lazy_init();
        if (g_yolo_use_disp_thread != 0 && g_disp_thread_run != 0) {
            yolo_publish_draw_dets(det_w, det_h);
        } else {
            yolo_rgn_update(det_w, det_h);
        }
    }

    {
        static td_u32 dbg_frame = 0;
        dbg_frame++;
        if ((dbg_frame % 60U) == 1U) {
            if (pick_n > 0) {
                printf("person_draw: det=%d pick=1 cls=%d score=%.3f box=(%.0f,%.0f,%.0f,%.0f) feat=%d det=%ux%u preview=%ux%u\n",
                    det_cnt, picked[0].cls_id, picked[0].score,
                    g_draw_dets[0].x1, g_draw_dets[0].y1, g_draw_dets[0].x2, g_draw_dets[0].y2,
                    det_feat, det_w, det_h, disp_w, disp_h);
            } else {
                printf("person_draw: det=%d pick=0 feat=%d det=%ux%u preview=%ux%u out_float=%u\n",
                    det_cnt, det_feat, det_w, det_h, disp_w, disp_h, (td_u32)out_float_num);
            }
        }
    }
}


static td_s32 ai_det_load(const char *path)
{
    td_s32 ret;

    if ((path == TD_NULL) || (path[0] == '\0') || (access(path, R_OK) != 0)) {
        printf("ai_det: model not found: %s\n", (path != TD_NULL) ? path : "(null)");
        return TD_FAILURE;
    }

    ret = aclmdlLoadFromFile(path, &g_det_model_id);
    if (ret != ACL_SUCCESS) {
        printf("ai_det: aclmdlLoadFromFile failed ret=%d path=%s\n", (int)ret, path);
        return TD_FAILURE;
    }

    g_det_model_desc = aclmdlCreateDesc();
    if (g_det_model_desc == TD_NULL) {
        (td_void)aclmdlUnload(g_det_model_id);
        g_det_model_id = 0;
        return TD_FAILURE;
    }
    ret = aclmdlGetDesc(g_det_model_desc, g_det_model_id);
    if (ret != ACL_SUCCESS) {
        aclmdlDestroyDesc(g_det_model_desc);
        g_det_model_desc = TD_NULL;
        (td_void)aclmdlUnload(g_det_model_id);
        g_det_model_id = 0;
        return TD_FAILURE;
    }

    g_det_enabled = 1;
    g_det_vpss_chn = (ot_vpss_chn)vio_ai_env_get_int_default("WIDGET_YOLO_DET_CHN", 2);
    {
        aclmdlIODims idims;
        aclmdlIODims odims;
        td_u32 i;
        (td_void)memset_s(&idims, sizeof(idims), 0, sizeof(idims));
        (td_void)memset_s(&odims, sizeof(odims), 0, sizeof(odims));
        printf("ai_det: loaded %s vpss_chn=%d det=640x640 -> preview\n", path, (int)g_det_vpss_chn);
        if (aclmdlGetInputDims(g_det_model_desc, 0, &idims) == ACL_SUCCESS) {
            printf("ai_det input dims=");
            for (i = 0; i < idims.dimCount; i++) {
                printf("%lld%s", (long long)idims.dims[i], (i + 1 == idims.dimCount) ? "" : "x");
            }
            printf("\n");
        }
        if (aclmdlGetOutputDims(g_det_model_desc, 0, &odims) == ACL_SUCCESS) {
            printf("ai_det output dims=");
            for (i = 0; i < odims.dimCount; i++) {
                printf("%lld%s", (long long)odims.dims[i], (i + 1 == odims.dimCount) ? "" : "x");
            }
            printf(" size=%zu\n", aclmdlGetOutputSizeByIndex(g_det_model_desc, 0));
        }
    }
    return TD_SUCCESS;
}

static td_void ai_det_unload(td_void)
{
    if (g_det_model_desc != TD_NULL) {
        aclmdlDestroyDesc(g_det_model_desc);
        g_det_model_desc = TD_NULL;
    }
    if (g_det_model_id != 0) {
        (td_void)aclmdlUnload(g_det_model_id);
        g_det_model_id = 0;
    }
    g_det_enabled = 0;
}

td_s32 ai_infer_from_nv12(const ot_video_frame_info *frame_info);

static td_s32 ai_det_infer_person_only(const ot_video_frame_info *frame_info)
{
    aclmdlDesc *saved_desc = g_model_desc;
    uint32_t saved_id = g_model_id;
    int saved_post = g_infer_post_mode;
    td_s32 ret;

    if ((g_det_enabled == 0) || (g_det_model_desc == TD_NULL) || (frame_info == TD_NULL)) {
        return TD_FAILURE;
    }

    g_model_desc = g_det_model_desc;
    g_model_id = g_det_model_id;
    g_infer_post_mode = YOLO_INFER_POST_PERSON;
    ret = ai_infer_from_nv12(frame_info);
    g_model_desc = saved_desc;
    g_model_id = saved_id;
    g_infer_post_mode = saved_post;
    return ret;
}


static td_s32 ai_init(const char *om_path)
{
    td_s32 ret;
    if (om_path != TD_NULL) {
        g_model_path = om_path;
    }

    ret = aclInit("");
    ACL_CHK(ret, "aclInit");
    ret = aclrtSetDevice(0);
    ACL_CHK(ret, "aclrtSetDevice");

    ret = aclmdlLoadFromFile(g_model_path, &g_model_id);
    ACL_CHK(ret, "aclmdlLoadFromFile");

    g_model_desc = aclmdlCreateDesc();
    if (g_model_desc == TD_NULL) {
        printf("aclmdlCreateDesc failed\n");
        return TD_FAILURE;
    }
    ret = aclmdlGetDesc(g_model_desc, g_model_id);
    ACL_CHK(ret, "aclmdlGetDesc");

    g_ai_cls_mode = ai_detect_cls_mode();
    printf("ai_init: model=%s cls_mode=%d classes=%d\n", g_model_path, g_ai_cls_mode, YOLO_NUM_CLASSES);

    if (g_ai_cls_mode != 0) {
        const char *det_path = getenv("WIDGET_YOLO_DET_MODEL");
        const char *det_dis = getenv("WIDGET_YOLO_DET_DISABLE");
        if (det_path == TD_NULL || det_path[0] == '\0') {
            det_path = "/opt/yolov8n.om";
        }
        if (g_yolo_box_draw == 0) {
            det_dis = "1";
        }
        if (det_dis == TD_NULL || det_dis[0] == '\0' || det_dis[0] == '0') {
            (td_void)ai_det_load(det_path);
        }
    }

    {
        if (vio_ai_env_get_int_default("WIDGET_POSE_ENABLE", 0) != 0) {
            const char *pose_path = getenv("WIDGET_POSE_MODEL");
            if (pose_path == TD_NULL || pose_path[0] == '\0') {
                pose_path = "/opt/widget_ui/models/best_pose_aipp.om";
            }
            (td_void)ai_pose_load(pose_path);
        } else {
            printf("pose: disabled (WIDGET_POSE_ENABLE=0)\n");
        }
    }

    return TD_SUCCESS;
}

static td_void ai_deinit(td_void)
{
    ai_pose_unload();
    ai_det_unload();
    if (g_input_dataset != TD_NULL) {
        aclmdlDestroyDataset(g_input_dataset);
        g_input_dataset = TD_NULL;
    }
    if (g_output_dataset != TD_NULL) {
        aclmdlDestroyDataset(g_output_dataset);
        g_output_dataset = TD_NULL;
    }
    if (g_model_desc != TD_NULL) {
        aclmdlDestroyDesc(g_model_desc);
        g_model_desc = TD_NULL;
    }
    if (g_model_id != 0) {
        (td_void)aclmdlUnload(g_model_id);
        g_model_id = 0;
    }
    (td_void)aclrtResetDevice(0);
    (td_void)aclFinalize();
}

td_s32 ai_infer_from_nv12(const ot_video_frame_info *frame_info)
{
    td_s32 ret;
    void *input_dev = TD_NULL;
    void *output_dev = TD_NULL;
    void *input_host = TD_NULL;
    void *output_host = TD_NULL;
    float *output_fp32 = TD_NULL;
    size_t output_fp32_num = 0;
    aclDataType out_dtype;
    aclDataType in_dtype;
    void *frame_ptr = TD_NULL;
    td_bool need_unmap = TD_FALSE;
    void *y_mapped = TD_NULL;
    void *uv_mapped = TD_NULL;
    void *frame_ptr_contig = TD_NULL;
    aclDataBuffer *in_buf = TD_NULL;
    aclDataBuffer *out_buf = TD_NULL;

    if (frame_info == TD_NULL) {
        return TD_FAILURE;
    }

    /* NOTE: UV plane stride can differ from Y stride on some pipelines. */
    size_t y_sz = (size_t)frame_info->video_frame.stride[0] * frame_info->video_frame.height;
    size_t uv_sz = (size_t)frame_info->video_frame.stride[1] * (frame_info->video_frame.height / 2);
    size_t frame_size = y_sz + uv_sz;
    size_t in_size = aclmdlGetInputSizeByIndex(g_model_desc, 0);
    size_t out_size = aclmdlGetOutputSizeByIndex(g_model_desc, 0);
    size_t copy_size = 0;
    td_u32 net_w = 640;
    td_u32 net_h = 640;
    in_dtype = aclmdlGetInputDataType(g_model_desc, 0);

    frame_ptr = frame_info->video_frame.virt_addr[0];
    if (frame_ptr == TD_NULL) {
        /* virt_addr[0]/[1] are not mapped: map Y and UV separately,
         * then pack into a contiguous buffer to avoid relying on a single contiguous mapping. */
        y_mapped = ss_mpi_sys_mmap(frame_info->video_frame.phys_addr[0], y_sz);
        if (y_mapped == TD_NULL) {
            printf("ss_mpi_sys_mmap failed Y phy=0x%llx size=%zu\n",
                (unsigned long long)frame_info->video_frame.phys_addr[0], y_sz);
            return TD_FAILURE;
        }
        uv_mapped = ss_mpi_sys_mmap(frame_info->video_frame.phys_addr[1], uv_sz);
        if (uv_mapped == TD_NULL) {
            printf("ss_mpi_sys_mmap failed UV phy=0x%llx size=%zu\n",
                (unsigned long long)frame_info->video_frame.phys_addr[1], uv_sz);
            (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            y_mapped = TD_NULL;
            return TD_FAILURE;
        }

        frame_ptr_contig = malloc(frame_size);
        if (frame_ptr_contig == TD_NULL) {
            printf("malloc frame_ptr_contig failed size=%zu\n", frame_size);
            (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            y_mapped = TD_NULL;
            uv_mapped = TD_NULL;
            return TD_FAILURE;
        }
        (void)memcpy(frame_ptr_contig, y_mapped, y_sz);
        (void)memcpy((unsigned char *)frame_ptr_contig + y_sz, uv_mapped, uv_sz);
        frame_ptr = frame_ptr_contig;
        need_unmap = TD_TRUE;
    }
    {
        static td_bool s_frame_ptr_dbg_printed = TD_FALSE;
        if (s_frame_ptr_dbg_printed == TD_FALSE) {
            printf("yolo frame_ptr dbg: virt0=%p virt1=%p phys0=0x%llx phys1=0x%llx need_unmap=%d y_sz=%zu uv_sz=%zu pf=%u comp=%d vfmt=%d dyn=%d\n",
                frame_info->video_frame.virt_addr[0],
                frame_info->video_frame.virt_addr[1],
                (unsigned long long)frame_info->video_frame.phys_addr[0],
                (unsigned long long)frame_info->video_frame.phys_addr[1],
                (int)need_unmap, y_sz, uv_sz,
                (td_u32)frame_info->video_frame.pixel_format,
                (int)frame_info->video_frame.compress_mode,
                (int)frame_info->video_frame.video_format,
                (int)frame_info->video_frame.dynamic_range);
            s_frame_ptr_dbg_printed = TD_TRUE;
        }
    }

    g_input_dataset = aclmdlCreateDataset();
    g_output_dataset = aclmdlCreateDataset();
    if ((g_input_dataset == TD_NULL) || (g_output_dataset == TD_NULL)) {
        if (need_unmap == TD_TRUE) {
            if (y_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            if (uv_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            if (frame_ptr_contig != TD_NULL) free(frame_ptr_contig);
        }
        return TD_FAILURE;
    }

    ret = aclrtMalloc(&input_dev, in_size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        printf("aclrtMalloc input failed ret=%d in_size=%zu\n", (int)ret, in_size);
        return TD_FAILURE;
    }
    ret = aclrtMalloc(&output_dev, out_size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        printf("aclrtMalloc output failed ret=%d out_size=%zu\n", (int)ret, out_size);
        (td_void)aclrtFree(input_dev);
        return TD_FAILURE;
    }

    ret = aclrtMemset(input_dev, in_size, 0, in_size);
    if (ret != ACL_SUCCESS) {
        printf("aclrtMemset failed ret=%d in_size=%zu\n", (int)ret, in_size);
        (td_void)aclrtFree(input_dev);
        (td_void)aclrtFree(output_dev);
        return TD_FAILURE;
    }

    if (prepare_model_input_from_nv12(frame_info, frame_ptr, &input_host, &copy_size, &net_w, &net_h, in_size, in_dtype) != TD_TRUE) {
        printf("prepare_model_input_from_nv12 failed src=%ux%u stride=%u in_size=%zu\n",
            frame_info->video_frame.width, frame_info->video_frame.height,
            frame_info->video_frame.stride[0], in_size);
        (td_void)aclrtFree(input_dev);
        (td_void)aclrtFree(output_dev);
        if (need_unmap == TD_TRUE) {
            if (y_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            if (uv_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            if (frame_ptr_contig != TD_NULL) free(frame_ptr_contig);
        }
        return TD_FAILURE;
    }
    if (copy_size > in_size) {
        copy_size = in_size;
    }

    ret = aclrtMemcpy(input_dev, in_size, input_host, copy_size, ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        printf("aclrtMemcpy failed ret=%d frame=%zu copy=%zu\n", (int)ret, frame_size, copy_size);
        free(input_host);
        (td_void)aclrtFree(input_dev);
        (td_void)aclrtFree(output_dev);
        if (need_unmap == TD_TRUE) {
            if (y_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            if (uv_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            if (frame_ptr_contig != TD_NULL) free(frame_ptr_contig);
        }
        return TD_FAILURE;
    }

    in_buf = aclCreateDataBuffer(input_dev, in_size);
    out_buf = aclCreateDataBuffer(output_dev, out_size);
    if ((in_buf == TD_NULL) || (out_buf == TD_NULL)) {
        if (in_buf != TD_NULL) {
            aclDestroyDataBuffer(in_buf);
        }
        if (out_buf != TD_NULL) {
            aclDestroyDataBuffer(out_buf);
        }
        (td_void)aclrtFree(input_dev);
        (td_void)aclrtFree(output_dev);
        aclmdlDestroyDataset(g_input_dataset);
        aclmdlDestroyDataset(g_output_dataset);
        g_input_dataset = TD_NULL;
        g_output_dataset = TD_NULL;
        if (need_unmap == TD_TRUE) {
            if (y_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            if (uv_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            if (frame_ptr_contig != TD_NULL) free(frame_ptr_contig);
        }
        return TD_FAILURE;
    }

    (td_void)aclmdlAddDatasetBuffer(g_input_dataset, in_buf);
    (td_void)aclmdlAddDatasetBuffer(g_output_dataset, out_buf);

    ret = aclmdlExecute(g_model_id, g_input_dataset, g_output_dataset);
    if (ret != ACL_SUCCESS) {
        printf("aclmdlExecute failed ret=%d input_size=%zu output_size=%zu copy=%zu\n",
            (int)ret, in_size, out_size, copy_size);
    } else {
        static td_bool s_meta_printed = TD_FALSE;
        out_dtype = aclmdlGetOutputDataType(g_model_desc, 0);
        if (s_meta_printed == TD_FALSE) {
            aclmdlIODims odims;
            aclmdlIODims idims;
            td_u32 i;
            (td_void)memset_s(&odims, sizeof(odims), 0, sizeof(odims));
            (td_void)memset_s(&idims, sizeof(idims), 0, sizeof(idims));
            if (aclmdlGetInputDims(g_model_desc, 0, &idims) == ACL_SUCCESS) {
                printf("yolo input meta: dtype=%d in_size=%zu src=%ux%u stride0=%u stride1=%u pf=%u comp=%d vfmt=%d src_nv21=%d prep=%ux%u dst_nv21=%d dims=",
                    (int)in_dtype, in_size, frame_info->video_frame.width, frame_info->video_frame.height,
                    frame_info->video_frame.stride[0], frame_info->video_frame.stride[1],
                    (td_u32)frame_info->video_frame.pixel_format,
                    (int)frame_info->video_frame.compress_mode,
                    (int)frame_info->video_frame.video_format,
                    vio_ai_pixel_format_is_nv21((td_u32)frame_info->video_frame.pixel_format),
                    net_w, net_h, (g_ab_feed_nv12 != 0) ? 0 : 1);
                for (i = 0; i < idims.dimCount; i++) {
                    printf("%u%s", (td_u32)idims.dims[i], (i + 1 == idims.dimCount) ? "" : "x");
                }
                printf("\n");
            }
            if (aclmdlGetOutputDims(g_model_desc, 0, &odims) == ACL_SUCCESS) {
                printf("yolo meta: out_dtype=%d out_size=%zu dimCount=%u dims=",
                    (int)out_dtype, out_size, (td_u32)odims.dimCount);
                for (i = 0; i < odims.dimCount; i++) {
                    printf("%llu%s",
                        (unsigned long long)odims.dims[i],
                        (i + 1 == odims.dimCount) ? "" : "x");
                }
                printf("\n");
            } else {
                printf("yolo meta: out_dtype=%d out_size=%zu (get dims failed)\n", (int)out_dtype, out_size);
            }
            s_meta_printed = TD_TRUE;
        }

        output_host = malloc(out_size);
        if (output_host != TD_NULL) {
            ret = aclrtMemcpy(output_host, out_size, output_dev, out_size, ACL_MEMCPY_DEVICE_TO_HOST);
            if (ret == ACL_SUCCESS) {
                if (convert_output_to_fp32(output_host, out_size, out_dtype, &output_fp32, &output_fp32_num) == TD_TRUE) {
                    if (g_infer_post_mode == YOLO_INFER_POST_PERSON) {
                        yolo_postprocess_person_draw_only(output_fp32, output_fp32_num,
                            frame_info->video_frame.width, frame_info->video_frame.height);
                    } else if (g_infer_post_mode == YOLO_INFER_POST_POSE) {
                        pose_postprocess_and_draw(output_fp32, output_fp32_num,
                            frame_info->video_frame.width, frame_info->video_frame.height);
                    } else if ((g_ai_cls_mode != 0) || (output_fp32_num == (size_t)YOLO_NUM_CLASSES)) {
                        if (g_ai_cls_mode == 0) {
                            g_ai_cls_mode = 1;
                        }
                        cls_postprocess_and_export(output_fp32, output_fp32_num,
                            frame_info->video_frame.width, frame_info->video_frame.height);
                    } else {
                        yolo_postprocess_and_dump(output_fp32, output_fp32_num,
                            frame_info->video_frame.width, frame_info->video_frame.height, 0);
                        yolo_rgn_lazy_init();
                        if (g_yolo_use_disp_thread != 0 && g_disp_thread_run != 0) {
                            yolo_publish_draw_dets(frame_info->video_frame.width, frame_info->video_frame.height);
                        } else {
                            yolo_rgn_update(frame_info->video_frame.width, frame_info->video_frame.height);
                        }
                        if (g_yolo_draw_nv12 != 0) {
                            draw_dets_on_nv12((unsigned char *)frame_ptr, frame_info->video_frame.stride[0],
                                frame_info->video_frame.width, frame_info->video_frame.height);
                        }
                    }
                    free(output_fp32);
                    output_fp32 = TD_NULL;
                } else {
                    printf("convert output to fp32 failed dtype=%d out_size=%zu\n", (int)out_dtype, out_size);
                }
            } else {
                printf("aclrtMemcpy D2H failed ret=%d out_size=%zu\n", (int)ret, out_size);
            }
            free(output_host);
            output_host = TD_NULL;
        } else {
            printf("malloc output_host failed size=%zu\n", out_size);
        }
    }

    /* per-frame cleanup */
    aclDestroyDataBuffer(in_buf);
    aclDestroyDataBuffer(out_buf);
    if (input_host != TD_NULL) {
        free(input_host);
    }
    (td_void)aclrtFree(input_dev);
    (td_void)aclrtFree(output_dev);
    aclmdlDestroyDataset(g_input_dataset);
    aclmdlDestroyDataset(g_output_dataset);
    g_input_dataset = TD_NULL;
    g_output_dataset = TD_NULL;
    if (need_unmap == TD_TRUE) {
        if (y_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
        if (uv_mapped != TD_NULL) (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
        if (frame_ptr_contig != TD_NULL) free(frame_ptr_contig);
    }

    return (ret == ACL_SUCCESS) ? TD_SUCCESS : TD_FAILURE;
}

static td_s32 ai_infer_offline_once(td_void)
{
    td_s32 ret;
    td_u32 i;
    td_u32 in_num;
    td_u32 out_num;
    void *input_host = TD_NULL;
    void *input_dev[8] = {TD_NULL};
    void *output_dev[8] = {TD_NULL};
    size_t in_size[8] = {0};
    size_t out_size[8] = {0};
    aclmdlDataset *in_dataset = TD_NULL;
    aclmdlDataset *out_dataset = TD_NULL;
    aclDataBuffer *buf;
    const char *offline_input = TD_NULL;
    FILE *fp = TD_NULL;
    size_t read_n = 0;
    void *out_host = TD_NULL;
    float *out_fp32 = TD_NULL;
    size_t out_fp32_num = 0;
    aclDataType out_dtype0 = ACL_DT_UNDEFINED;

    in_num = aclmdlGetNumInputs(g_model_desc);
    out_num = aclmdlGetNumOutputs(g_model_desc);
    if ((in_num == 0) || (out_num == 0) || (in_num > 8) || (out_num > 8)) {
        printf("offline: invalid io num in=%u out=%u\n", in_num, out_num);
        return TD_FAILURE;
    }

    for (i = 0; i < in_num; i++) {
        in_size[i] = aclmdlGetInputSizeByIndex(g_model_desc, i);
    }
    for (i = 0; i < out_num; i++) {
        out_size[i] = aclmdlGetOutputSizeByIndex(g_model_desc, i);
    }

    input_host = calloc(1, in_size[0]);
    if (input_host == TD_NULL) {
        printf("offline: calloc failed size=%zu\n", in_size[0]);
        return TD_FAILURE;
    }
    offline_input = getenv("YOLO_OFFLINE_INPUT");
    if ((offline_input != TD_NULL) && (offline_input[0] != '\0')) {
        fp = fopen(offline_input, "rb");
        if (fp == TD_NULL) {
            printf("offline: failed to open input file %s\n", offline_input);
            ret = TD_FAILURE;
            goto offline_fail;
        }
        read_n = fread(input_host, 1, in_size[0], fp);
        (td_void)fclose(fp);
        fp = TD_NULL;
        if (read_n != in_size[0]) {
            printf("offline: input file size mismatch path=%s read=%zu expect=%zu\n",
                offline_input, read_n, in_size[0]);
            ret = TD_FAILURE;
            goto offline_fail;
        }
        printf("offline: loaded input %s (%zu bytes)\n", offline_input, read_n);
    } else {
        printf("offline: YOLO_OFFLINE_INPUT not set, using zero input\n");
    }

    in_dataset = aclmdlCreateDataset();
    out_dataset = aclmdlCreateDataset();
    if ((in_dataset == TD_NULL) || (out_dataset == TD_NULL)) {
        printf("offline: create dataset failed\n");
        free(input_host);
        if (in_dataset != TD_NULL) {
            aclmdlDestroyDataset(in_dataset);
        }
        if (out_dataset != TD_NULL) {
            aclmdlDestroyDataset(out_dataset);
        }
        return TD_FAILURE;
    }

    for (i = 0; i < in_num; i++) {
        ret = aclrtMalloc(&input_dev[i], in_size[i], ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) {
            printf("offline: aclrtMalloc input[%u] failed ret=%d size=%zu\n", i, (int)ret, in_size[i]);
            goto offline_fail;
        }
        if (i == 0) {
            ret = aclrtMemcpy(input_dev[i], in_size[i], input_host, in_size[i], ACL_MEMCPY_HOST_TO_DEVICE);
        } else {
            ret = aclrtMemset(input_dev[i], in_size[i], 0, in_size[i]);
        }
        if (ret != ACL_SUCCESS) {
            printf("offline: init input[%u] failed ret=%d\n", i, (int)ret);
            goto offline_fail;
        }
        buf = aclCreateDataBuffer(input_dev[i], in_size[i]);
        if (buf == TD_NULL) {
            printf("offline: create input buffer[%u] failed\n", i);
            goto offline_fail;
        }
        (td_void)aclmdlAddDatasetBuffer(in_dataset, buf);
    }

    for (i = 0; i < out_num; i++) {
        ret = aclrtMalloc(&output_dev[i], out_size[i], ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) {
            printf("offline: aclrtMalloc output[%u] failed ret=%d size=%zu\n", i, (int)ret, out_size[i]);
            goto offline_fail;
        }
        buf = aclCreateDataBuffer(output_dev[i], out_size[i]);
        if (buf == TD_NULL) {
            printf("offline: create output buffer[%u] failed\n", i);
            goto offline_fail;
        }
        (td_void)aclmdlAddDatasetBuffer(out_dataset, buf);
    }

    free(input_host);
    input_host = TD_NULL;

    ret = aclmdlExecute(g_model_id, in_dataset, out_dataset);
    printf("offline: aclmdlExecute ret=%d in_num=%u out_num=%u\n", (int)ret, in_num, out_num);
    if ((ret == ACL_SUCCESS) && (out_num > 0)) {
        out_dtype0 = aclmdlGetOutputDataType(g_model_desc, 0);
        out_host = malloc(out_size[0]);
        if (out_host != TD_NULL) {
            ret = aclrtMemcpy(out_host, out_size[0], output_dev[0], out_size[0], ACL_MEMCPY_DEVICE_TO_HOST);
            if (ret == ACL_SUCCESS) {
                if (convert_output_to_fp32(out_host, out_size[0], out_dtype0, &out_fp32, &out_fp32_num) == TD_TRUE) {
                    g_preproc_meta.scale = 1.0f;
                    g_preproc_meta.pad_x = 0.0f;
                    g_preproc_meta.pad_y = 0.0f;
                    g_preproc_meta.net_w = 640;
                    g_preproc_meta.net_h = 640;
                    g_preproc_meta.img_w = 640;
                    g_preproc_meta.img_h = 640;
                    yolo_postprocess_and_dump(out_fp32, out_fp32_num, 640, 640, 0);
                    free(out_fp32);
                    out_fp32 = TD_NULL;
                } else {
                    printf("offline: convert output0 to fp32 failed dtype=%d size=%zu\n", (int)out_dtype0, out_size[0]);
                }
            } else {
                printf("offline: D2H memcpy failed ret=%d size=%zu\n", (int)ret, out_size[0]);
            }
            free(out_host);
            out_host = TD_NULL;
        }
    }

offline_fail:
    if (fp != TD_NULL) {
        (td_void)fclose(fp);
        fp = TD_NULL;
    }
    if (out_fp32 != TD_NULL) {
        free(out_fp32);
        out_fp32 = TD_NULL;
    }
    if (out_host != TD_NULL) {
        free(out_host);
        out_host = TD_NULL;
    }
    if (input_host != TD_NULL) {
        free(input_host);
        input_host = TD_NULL;
    }
    if (in_dataset != TD_NULL) {
        for (i = 0; i < aclmdlGetDatasetNumBuffers(in_dataset); i++) {
            aclDataBuffer *b = aclmdlGetDatasetBuffer(in_dataset, i);
            if (b != TD_NULL) {
                aclDestroyDataBuffer(b);
            }
        }
        aclmdlDestroyDataset(in_dataset);
    }
    if (out_dataset != TD_NULL) {
        for (i = 0; i < aclmdlGetDatasetNumBuffers(out_dataset); i++) {
            aclDataBuffer *b = aclmdlGetDatasetBuffer(out_dataset, i);
            if (b != TD_NULL) {
                aclDestroyDataBuffer(b);
            }
        }
        aclmdlDestroyDataset(out_dataset);
    }
    for (i = 0; i < 8; i++) {
        if (input_dev[i] != TD_NULL) {
            (td_void)aclrtFree(input_dev[i]);
        }
        if (output_dev[i] != TD_NULL) {
            (td_void)aclrtFree(output_dev[i]);
        }
    }

    return (ret == ACL_SUCCESS) ? TD_SUCCESS : TD_FAILURE;
}

/*
 * Bring in sample_vio.c as-is.
 * - Rename its main() to avoid duplicate symbol.
 * - We will call its static helper(s) inside this translation unit.
 */
#define main sample_vio_original_main
#include "../vio/sample_vio.c"
#undef main

static td_bool sample_vio_ai_probe_any_live_target(ot_vpss_grp *out_grp, ot_vpss_chn *out_chn)
{
    int g;
    int c;
    int pass;
    ot_video_frame_info frame;
    td_s32 ret;
    ot_vpss_chn_attr chn_attr;

    if ((out_grp == TD_NULL) || (out_chn == TD_NULL)) {
        return TD_FALSE;
    }

    for (pass = 0; pass < 3; pass++) {
        for (g = 0; g < 1; g++) {
            for (c = 0; c < 4; c++) {
                if (ss_mpi_vpss_get_chn_attr((ot_vpss_grp)g, (ot_vpss_chn)c, &chn_attr) != TD_SUCCESS) {
                    continue;
                }
                if ((pass == 0) && !((chn_attr.width == 640U) && (chn_attr.height == 640U))) {
                    continue;
                }
                if ((pass == 1) &&
                    !((chn_attr.width <= 1920U) && (chn_attr.height <= 1080U) &&
                      (chn_attr.width >= 640U) && (chn_attr.height >= 360U))) {
                    continue;
                }
                if ((pass == 2) &&
                    ((chn_attr.width <= 1920U) && (chn_attr.height <= 1080U) &&
                     (chn_attr.width >= 640U) && (chn_attr.height >= 360U))) {
                    continue;
                }
                (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
                ret = ss_mpi_vpss_get_chn_frame((ot_vpss_grp)g, (ot_vpss_chn)c, &frame, 50);
                if (ret == TD_SUCCESS) {
                    (td_void)ss_mpi_vpss_release_chn_frame((ot_vpss_grp)g, (ot_vpss_chn)c, &frame);
                    *out_grp = (ot_vpss_grp)g;
                    *out_chn = (ot_vpss_chn)c;
                    return TD_TRUE;
                }
            }
        }
    }
    return TD_FALSE;
}

static td_bool sample_vio_ai_runtime_recover_target(ot_vpss_grp *out_grp, ot_vpss_chn *out_chn)
{
    int g;
    int c;
    int pass;
    td_bool depth_changed = TD_FALSE;
    td_u32 old_depth = 0;
    ot_vpss_chn_attr chn_attr;

    if ((out_grp == TD_NULL) || (out_chn == TD_NULL)) {
        return TD_FALSE;
    }

    for (pass = 0; pass < 3; pass++) {
        for (g = 0; g < 1; g++) {
            for (c = 0; c < 4; c++) {
                if (ss_mpi_vpss_get_chn_attr((ot_vpss_grp)g, (ot_vpss_chn)c, &chn_attr) != TD_SUCCESS) {
                    continue;
                }
                if ((pass == 0) && !((chn_attr.width == 640U) && (chn_attr.height == 640U))) {
                    continue;
                }
                if ((pass == 1) &&
                    !((chn_attr.width <= 1920U) && (chn_attr.height <= 1080U) &&
                      (chn_attr.width >= 640U) && (chn_attr.height >= 360U))) {
                    continue;
                }
                if ((pass == 2) &&
                    ((chn_attr.width <= 1920U) && (chn_attr.height <= 1080U) &&
                     (chn_attr.width >= 640U) && (chn_attr.height >= 360U))) {
                    continue;
                }
                if (sample_vio_ai_try_attach_target((ot_vpss_grp)g, (ot_vpss_chn)c, &depth_changed, &old_depth) == TD_TRUE) {
                    *out_grp = (ot_vpss_grp)g;
                    *out_chn = (ot_vpss_chn)c;
                    return TD_TRUE;
                }
            }
        }
    }
    return TD_FALSE;
}

static td_bool sample_vio_ai_recover_current_target(ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_bool depth_changed = TD_FALSE;
    td_u32 old_depth = 0;

    if (sample_vio_ai_try_attach_target(grp, chn, &depth_changed, &old_depth) == TD_TRUE) {
        printf("runtime recover: current target recovered (%d,%d)\n", grp, chn);
        return TD_TRUE;
    }
    printf("runtime recover: current target still unavailable (%d,%d)\n", grp, chn);
    return TD_FALSE;
}


static td_void vio_ai_loop(ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_s32 ret;
    td_s32 infer_ret;
    ot_video_frame_info frame;
    td_u32 frame_fail_cnt = 0;
    td_u32 infer_fail_cnt = 0;
    td_u32 ok_cnt = 0;
    td_u32 fps_cnt = 0;
    struct timeval fps_t0 = {0, 0};
    struct timeval fps_t1 = {0, 0};
    ot_vpss_grp cur_grp = grp;
    ot_vpss_chn cur_chn = chn;

    yolo_tune_init_once();
    (void)gettimeofday(&fps_t0, TD_NULL);
    while (g_sig_flag == 0) {
        if (g_pose_enabled != 0) {
            pose_rgn_expire_if_stale();
        }

        ret = ss_mpi_vpss_get_chn_frame(cur_grp, cur_chn, &frame, g_yolo_vpss_get_ms);
        if (ret != TD_SUCCESS) {
            frame_fail_cnt++;
            /* attach 模式勿抢 VI 帧，否则会饿死 VPSS/VO 预览导致几秒后黑屏 */
            if (g_attach_pipeline_mode != TD_FALSE) {
                if ((frame_fail_cnt <= 5) || (frame_fail_cnt % 100 == 0)) {
                    printf("attach vpss_get_frame failed: grp=%d chn=%d ret=0x%x cnt=%u\n",
                        cur_grp, cur_chn, (td_u32)ret, frame_fail_cnt);
                }
                if (frame_fail_cnt >= 120) {
                    printf("attach: vpss chn starved %u times, exit for watchdog restart\n", frame_fail_cnt);
                    exit(42);
                }
                usleep(50000);
                continue;
            }
            if ((frame_fail_cnt <= 10) || (frame_fail_cnt % 100 == 0)) {
                ot_video_frame_info vi_probe;
                td_s32 vi_ret;
                (td_void)memset_s(&vi_probe, sizeof(vi_probe), 0, sizeof(vi_probe));
                vi_ret = ss_mpi_vi_get_chn_frame(g_active_vi_pipe, 0, &vi_probe, 50);
                printf("get_chn_frame failed: grp=%d chn=%d ret=0x%x cnt=%u vi_probe=0x%x\n",
                    cur_grp, cur_chn, (td_u32)ret, frame_fail_cnt, (td_u32)vi_ret);
                if (vi_ret == TD_SUCCESS) {
                    (td_void)ss_mpi_vi_release_chn_frame(g_active_vi_pipe, 0, &vi_probe);
                }
            }
            if ((g_attach_pipeline_mode == TD_FALSE) && (frame_fail_cnt >= 10) && ((frame_fail_cnt % 10) == 0)) {
                ot_vpss_grp new_grp = cur_grp;
                ot_vpss_chn new_chn = cur_chn;
                if (sample_vio_ai_probe_any_live_target(&new_grp, &new_chn) == TD_TRUE) {
                    if ((new_grp != cur_grp) || (new_chn != cur_chn)) {
                        printf("frame source switch: (%d,%d) -> (%d,%d)\n",
                            cur_grp, cur_chn, new_grp, new_chn);
                        cur_grp = new_grp;
                        cur_chn = new_chn;
                        g_attach_grp = cur_grp;
                        g_attach_chn = cur_chn;
                    }
                    frame_fail_cnt = 0;
                }
            }
            if ((g_attach_pipeline_mode == TD_FALSE) && (frame_fail_cnt >= 50) && ((frame_fail_cnt % 50) == 0)) {
                printf("runtime recover: retry current target, cnt=%u\n", frame_fail_cnt);
                if (sample_vio_ai_recover_current_target(cur_grp, cur_chn) == TD_TRUE) {
                    frame_fail_cnt = 0;
                    continue;
                }
            }
            if ((g_attach_pipeline_mode == TD_FALSE) && (frame_fail_cnt >= 200) && ((frame_fail_cnt % 200) == 0)) {
                ot_vpss_grp new_grp = cur_grp;
                ot_vpss_chn new_chn = cur_chn;
                printf("runtime recover: deep reprobe start, cnt=%u\n", frame_fail_cnt);
                if (sample_vio_ai_runtime_recover_target(&new_grp, &new_chn) == TD_TRUE) {
                    printf("runtime recover: use target (%d,%d)\n", new_grp, new_chn);
                    cur_grp = new_grp;
                    cur_chn = new_chn;
                    g_attach_grp = cur_grp;
                    g_attach_chn = cur_chn;
                    frame_fail_cnt = 0;
                } else {
                    printf("runtime recover: deep reprobe no target\n");
                }
            }
            usleep(10000);
            continue;
        }

        infer_ret = TD_FAILURE;
        {
            vio_ai_owned_frame_t owned;

            if (vio_ai_own_frame(&frame, &owned) == TD_SUCCESS) {
                (td_void)ss_mpi_vpss_release_chn_frame(cur_grp, cur_chn, &frame);
                hit_replay_poll_trigger();
                hit_replay_poll_pose_trigger();
                infer_ret = ai_infer_from_nv12(&owned.info);
                if ((infer_ret == TD_SUCCESS) && (g_pose_enabled != 0) &&
                    (g_attach_pipeline_mode != TD_FALSE) && (g_pose_ch1_only != 0)) {
                    static td_u32 s_pose_skip_ch1;

                    s_pose_skip_ch1++;
                    if ((s_pose_skip_ch1 % g_pose_infer_interval) == 0U) {
                        (td_void)ai_pose_infer(&owned.info);
                    }
                }
                if (hit_replay_uses_dedicated_chn() != TD_TRUE) {
                    hit_replay_submit_frame(&owned.info);
                } else {
                    hit_replay_submit_frame(TD_NULL);
                }
                vio_ai_disown_frame(&owned);
            } else {
                (td_void)ss_mpi_vpss_release_chn_frame(cur_grp, cur_chn, &frame);
            }
        }
        if (infer_ret != TD_SUCCESS) {
            infer_fail_cnt++;
            if ((infer_fail_cnt <= 10) || (infer_fail_cnt % 100 == 0)) {
                printf("ai_infer failed: ret=0x%x cnt=%u\n", (td_u32)infer_ret, infer_fail_cnt);
            }
        } else if ((g_det_enabled != 0) ||
            ((g_pose_enabled != 0) && !((g_attach_pipeline_mode != TD_FALSE) && (g_pose_ch1_only != 0)))) {
            ot_video_frame_info det_frame;
            td_s32 det_ret;
            ot_vpss_chn aux_chn = g_pose_enabled ? g_pose_vpss_chn : g_det_vpss_chn;
            static td_u32 s_pose_skip_ch2;

            s_pose_skip_ch2++;
            if (g_pose_enabled != 0 && ((s_pose_skip_ch2 % g_pose_infer_interval) != 0U)) {
                /* 骨骼每 N 帧推理一次，减轻 ch2 缓冲压力 */
            } else {
            (td_void)memset_s(&det_frame, sizeof(det_frame), 0, sizeof(det_frame));
            det_ret = ss_mpi_vpss_get_chn_frame(cur_grp, aux_chn, &det_frame,
                (g_attach_pipeline_mode != TD_FALSE) ? 15 : g_yolo_vpss_get_ms);
            if (det_ret == TD_SUCCESS) {
                vio_ai_owned_frame_t det_owned;

                if (vio_ai_own_frame(&det_frame, &det_owned) == TD_SUCCESS) {
                    (td_void)ss_mpi_vpss_release_chn_frame(cur_grp, aux_chn, &det_frame);
                    if (g_det_enabled != 0) {
                        (td_void)ai_det_infer_person_only(&det_owned.info);
                    }
                    if (g_pose_enabled != 0) {
                        (td_void)ai_pose_infer(&det_owned.info);
                    }
                    vio_ai_disown_frame(&det_owned);
                } else {
                    (td_void)ss_mpi_vpss_release_chn_frame(cur_grp, aux_chn, &det_frame);
                }
            }
            }
        }
        pose_rgn_redraw_cached();
        ok_cnt++;
        fps_cnt++;
        frame_fail_cnt = 0;
        if ((fps_cnt >= 60) && (gettimeofday(&fps_t1, TD_NULL) == 0)) {
            double dt = (double)(fps_t1.tv_sec - fps_t0.tv_sec) +
                (double)(fps_t1.tv_usec - fps_t0.tv_usec) / 1000000.0;
            if (dt > 0.01) {
                printf("yolo infer fps: %.1f (ok_cnt=%u frame_ms=%d)\n",
                    (double)fps_cnt / dt, ok_cnt, g_yolo_vpss_get_ms);
            }
            fps_cnt = 0;
            fps_t0 = fps_t1;
        }
        if ((ok_cnt % 300) == 0) {
            printf("frame loop alive: ok_cnt=%u\n", ok_cnt);
        }
    }
}

static td_void sample_vio_ai_probe_frame_path(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn, ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_s32 ret;
    ot_video_frame_info probe_frame;

    (td_void)memset_s(&probe_frame, sizeof(probe_frame), 0, sizeof(probe_frame));

    sleep(1);

    ret = ss_mpi_vpss_get_chn_frame(grp, chn, &probe_frame, 3000);
    printf("probe vpss_get_frame ret=0x%x (grp=%d chn=%d)\n", (td_u32)ret, grp, chn);
    if (ret == TD_SUCCESS) {
        (td_void)ss_mpi_vpss_release_chn_frame(grp, chn, &probe_frame);
    }

    (td_void)memset_s(&probe_frame, sizeof(probe_frame), 0, sizeof(probe_frame));
    ret = ss_mpi_vi_get_chn_frame(vi_pipe, vi_chn, &probe_frame, 3000);
    printf("probe vi_get_frame ret=0x%x (pipe=%d chn=%d)\n", (td_u32)ret, vi_pipe, vi_chn);
    if (ret == TD_SUCCESS) {
        (td_void)ss_mpi_vi_release_chn_frame(vi_pipe, vi_chn, &probe_frame);
    }
}

static td_void sample_vio_ai_probe_vi_only(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn, const char *tag)
{
    td_s32 ret;
    ot_video_frame_info probe_frame;

    (td_void)memset_s(&probe_frame, sizeof(probe_frame), 0, sizeof(probe_frame));
    ret = ss_mpi_vi_get_chn_frame(vi_pipe, vi_chn, &probe_frame, 3000);
    printf("probe vi_get_frame[%s] ret=0x%x (pipe=%d chn=%d)\n", tag, (td_u32)ret, vi_pipe, vi_chn);
    if (ret == TD_SUCCESS) {
        (td_void)ss_mpi_vi_release_chn_frame(vi_pipe, vi_chn, &probe_frame);
    }
}

static td_s32 sample_vio_ai_start_vpss_with_log(ot_vpss_grp grp, ot_size *in_size)
{
    td_s32 ret;
    ot_low_delay_info low_delay_info;
    ot_vpss_grp_attr grp_attr;
    ot_vpss_chn_attr chn_attr;
    td_bool chn_enable[OT_VPSS_MAX_PHYS_CHN_NUM] = {TD_TRUE, TD_FALSE, TD_FALSE, TD_FALSE};

    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width = in_size->width;
    grp_attr.max_height = in_size->height;
    sample_comm_vpss_get_default_chn_attr(&chn_attr);
    chn_attr.width = in_size->width;
    chn_attr.height = in_size->height;
    chn_attr.depth = 4;
    /* 防止 VPSS 输出压缩帧（压缩会导致我们把数据当成 NV12 原始字节流时出现规律条纹）。 */
    chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;

    printf("vpss grp_attr: grp=%d max=%ux%u pixel_format=%d frame_rate=%d->%d\n",
        grp, grp_attr.max_width, grp_attr.max_height, grp_attr.pixel_format,
        grp_attr.frame_rate.src_frame_rate, grp_attr.frame_rate.dst_frame_rate);
    printf("vpss chn_attr: chn0=%ux%u pixel_format=%d depth=%u enable={%d,%d,%d,%d}\n",
        chn_attr.width, chn_attr.height, chn_attr.pixel_format, chn_attr.depth,
        chn_enable[0], chn_enable[1], chn_enable[2], chn_enable[3]);
    printf("vpss key: vpss_chn_enable[0]=%d vpss_chn_attr[0].depth=%u vpss_chn_attr[0].pixel_format=%d\n",
        chn_enable[0], chn_attr.depth, chn_attr.pixel_format);

    ret = sample_common_vpss_start(grp, chn_enable, &grp_attr, &chn_attr, OT_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != TD_SUCCESS) {
        printf("sample_common_vpss_start failed: ret=0x%x\n", (td_u32)ret);
        return ret;
    }
    printf("sample_common_vpss_start ret=0x%x\n", (td_u32)ret);

    /* keep identical behavior with sample_vio_start_vpss() */
    low_delay_info.enable = TD_TRUE;
    low_delay_info.line_cnt = 200;
    low_delay_info.one_buf_en = TD_FALSE;
    ret = ss_mpi_vpss_set_low_delay_attr(grp, 0, &low_delay_info);
    printf("ss_mpi_vpss_set_low_delay_attr ret=0x%x\n", (td_u32)ret);
    if (ret != TD_SUCCESS) {
        sample_common_vpss_stop(grp, chn_enable, OT_VPSS_MAX_PHYS_CHN_NUM);
        return ret;
    }

    return TD_SUCCESS;
}

static td_bool sample_vio_ai_wait_first_frame(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn, ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_u32 i;
    td_s32 vi_ret;
    td_s32 vpss_ret;
    ot_video_frame_info frame;

    for (i = 0; i < 30; i++) {
        (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        vi_ret = ss_mpi_vi_get_chn_frame(vi_pipe, vi_chn, &frame, 500);
        if (vi_ret == TD_SUCCESS) {
            (td_void)ss_mpi_vi_release_chn_frame(vi_pipe, vi_chn, &frame);
        }

        (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        vpss_ret = ss_mpi_vpss_get_chn_frame(grp, chn, &frame, 500);
        if (vpss_ret == TD_SUCCESS) {
            (td_void)ss_mpi_vpss_release_chn_frame(grp, chn, &frame);
            printf("first frame ready: vi=0x%x vpss=0x%x\n", (td_u32)vi_ret, (td_u32)vpss_ret);
            return TD_TRUE;
        }

        printf("first frame pending: vi=0x%x vpss=0x%x (try=%u)\n", (td_u32)vi_ret, (td_u32)vpss_ret, i + 1);
        usleep(200000);
    }

    return TD_FALSE;
}

static td_bool sample_vio_ai_wait_attach_frame(ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_u32 i;
    td_s32 vpss_ret;
    td_s32 vi_ret;
    ot_video_frame_info frame;
    ot_video_frame_info vi_probe;

    for (i = 0; i < 150; i++) {
        (td_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        vpss_ret = ss_mpi_vpss_get_chn_frame(grp, chn, &frame, 500);
        if (vpss_ret == TD_SUCCESS) {
            printf("attach first frame ready: grp=%d chn=%d try=%u\n", grp, chn, i + 1);
            (td_void)ss_mpi_vpss_release_chn_frame(grp, chn, &frame);
            return TD_TRUE;
        }

        (td_void)memset_s(&vi_probe, sizeof(vi_probe), 0, sizeof(vi_probe));
        vi_ret = ss_mpi_vi_get_chn_frame(g_active_vi_pipe, 0, &vi_probe, 50);
        if ((i == 0) || ((i + 1) % 10U) == 0U) {
            printf("attach first frame pending: grp=%d chn=%d vpss=0x%x vi=0x%x try=%u\n",
                grp, chn, (td_u32)vpss_ret, (td_u32)vi_ret, i + 1);
        }
        if (vi_ret == TD_SUCCESS) {
            (td_void)ss_mpi_vi_release_chn_frame(g_active_vi_pipe, 0, &vi_probe);
        }
        usleep(200000);
    }

    return TD_FALSE;
}

static td_bool sample_vio_ai_wait_vpss_chn_ready(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 max_sec)
{
    ot_vpss_chn_attr chn_attr;
    td_u32 i;
    td_u32 tries = max_sec * 5U;

    if (tries < 10U) {
        tries = 10U;
    }
    for (i = 0; i < tries; i++) {
        (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
        if (ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr) == TD_SUCCESS) {
            if (i > 0U) {
                printf("attach: vpss chn ready grp=%d chn=%d after %u tries\n", grp, chn, i + 1);
            }
            return TD_TRUE;
        }
        usleep(200000);
    }
    return TD_FALSE;
}

static td_bool sample_vio_ai_try_attach_target(ot_vpss_grp grp, ot_vpss_chn chn,
    td_bool *depth_changed, td_u32 *old_depth)
{
    td_s32 ret;
    ot_vpss_chn_attr chn_attr;
    td_u32 old_compress = 0;

    if ((depth_changed == TD_NULL) || (old_depth == TD_NULL)) {
        return TD_FALSE;
    }
    *depth_changed = TD_FALSE;
    *old_depth = 0;
    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));

    ret = ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr);
    if (ret != TD_SUCCESS) {
        printf("attach target probe: get chn attr failed grp=%d chn=%d ret=0x%x\n", grp, chn, (td_u32)ret);
        return TD_FALSE;
    }
    *old_depth = chn_attr.depth;
    old_compress = chn_attr.compress_mode;
    if ((g_attach_pipeline_mode == TD_FALSE) && (chn_attr.compress_mode != OT_COMPRESS_MODE_NONE)) {
        /* Real-time VPSS output must be uncompressed, otherwise we interpret compressed bytes as raw NV12
         * and the result becomes striped/noisy. */
        chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        ret = ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            printf("attach target probe: set compress_mode failed grp=%d chn=%d ret=0x%x (ignore and try)\n",
                grp, chn, (td_u32)ret);
        }
    }
    /* attach 模式勿改 depth：由 camera_pipe 启动时配置，运行时 set 易触发 0xa0078007 */
    if ((g_attach_pipeline_mode == TD_FALSE) && (chn_attr.depth < 4)) {
        chn_attr.depth = 4;
        ret = ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            /* Don't hard-fail: some vpss channels may be readable with existing depth.
               Failing here forces fallback to a different (possibly incompatible) target. */
            printf("attach target probe: set depth failed grp=%d chn=%d ret=0x%x (ignore and try)\n", grp, chn, (td_u32)ret);
        } else {
            *depth_changed = TD_TRUE;
        }
    }

    if (sample_vio_ai_wait_attach_frame(grp, chn) == TD_TRUE) {
        return TD_TRUE;
    }

    if (*depth_changed == TD_TRUE) {
        if (ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr) == TD_SUCCESS) {
            chn_attr.depth = *old_depth;
            (td_void)ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr);
        }
        *depth_changed = TD_FALSE;
    }
    return TD_FALSE;
}

static sample_sns_type sample_vio_ai_pick_sensor_type(td_void)
{
#ifdef OV_OS08A20_MIPI_8M_30FPS_12BIT
    return OV_OS08A20_MIPI_8M_30FPS_12BIT;
#else
    return SENSOR0_TYPE;
#endif
}

static td_s32 sample_vio_ai_sys_init(sample_sns_type sns_type, ot_vi_vpss_mode_type mode_type,
    ot_vi_video_mode video_mode, td_u32 yuv_cnt, td_u32 raw_cnt)
{
    td_s32 ret;
    ot_size size;
    ot_vb_cfg vb_cfg;
    td_u32 supplement_config;

    sample_comm_vi_get_size_by_sns_type(sns_type, &size);
    sample_vi_get_default_vb_config(&size, &vb_cfg, video_mode, yuv_cnt, raw_cnt);

    supplement_config = OT_VB_SUPPLEMENT_BNR_MOT_MASK;
    ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config);
    if (ret != TD_SUCCESS) {
        printf("sample_vio_ai_sys_init: sys_init_with_vb failed ret=0x%x\n", (td_u32)ret);
        return TD_FAILURE;
    }

    ret = sample_comm_vi_set_vi_vpss_mode(mode_type, video_mode);
    if (ret != TD_SUCCESS) {
        printf("sample_vio_ai_sys_init: set_vi_vpss_mode failed ret=0x%x\n", (td_u32)ret);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

/* Align startup path with linear route: pipe0/grp0/ISP0. */
static td_s32 sample_vio_ai_run_once(const char *om_path, td_s32 bus_id)
{
    td_s32 ret;
    td_u32 yuv_cnt;
    td_u32 raw_cnt;
    ot_vi_vpss_mode_type mode_type = OT_VI_ONLINE_VPSS_OFFLINE;
    ot_vi_video_mode video_mode = OT_VI_VIDEO_MODE_NORM;
    const ot_vi_pipe vi_pipe = 0;
    const ot_vi_chn vi_chn = 0;
    ot_vpss_grp vpss_grp[1] = {0};
    sample_vi_cfg vi_cfg;
    sample_sns_type sns_type = sample_vio_ai_pick_sensor_type();
    ot_size in_size;

    (td_void)om_path;

    /* force linear mapping; avoid entering WDR sensor init path */
    sample_vio_get_vi_vpss_mode_by_char('0', &mode_type, &video_mode);
    sample_vio_get_vb_blk_num_by_char('0', &yuv_cnt, &raw_cnt, TD_FALSE);
    printf("mode map: mode_type=%d video_mode=%d yuv_cnt=%u raw_cnt=%u sns_type=%d\n",
        mode_type, video_mode, yuv_cnt, raw_cnt, sns_type);

    ret = sample_vio_ai_sys_init(sns_type, mode_type, video_mode, yuv_cnt, raw_cnt);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    vi_cfg.sns_info.bus_id = bus_id;
    vi_cfg.bind_pipe.pipe_id[0] = vi_pipe;
    vi_cfg.grp_info.fusion_grp[0] = 0;
    vi_cfg.grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;
    printf("fixed cfg(from sample_vio): bus=%d dev=%d pipe=%d grp=%d clk_src=%d rst_src=%d\n",
        vi_cfg.sns_info.bus_id, vi_cfg.dev_info.vi_dev,
        vi_cfg.bind_pipe.pipe_id[0], vi_cfg.grp_info.fusion_grp[0],
        vi_cfg.sns_info.sns_clk_src, vi_cfg.sns_info.sns_rst_src);

    ret = sample_comm_vi_start_vi(&vi_cfg);
    printf("sample_comm_vi_start_vi ret=0x%x\n", (td_u32)ret);
    if (ret != TD_SUCCESS) {
        printf("start_vi failed ret=0x%x\n", (td_u32)ret);
        goto start_vi_failed;
    }

    g_active_vi_pipe = vi_pipe;
    sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], 0);
    ret = sample_vio_ai_start_vpss_with_log(vpss_grp[0], &in_size);
    if (ret != TD_SUCCESS) {
        printf("start_vpss failed ret=0x%x\n", (td_u32)ret);
        goto start_vpss_failed;
    }

    if (sample_vio_ai_wait_first_frame(vi_pipe, vi_chn, vpss_grp[0], 0) != TD_TRUE) {
        ret = TD_FAILURE;
        printf("no frame under current cfg, switch to next cfg\n");
        goto ai_init_failed;
    }

    ret = ai_init(om_path);
    if (ret != TD_SUCCESS) {
        printf("ai_init failed ret=0x%x\n", (td_u32)ret);
        goto ai_init_failed;
    }

    vio_ai_loop(vpss_grp[0], 0);

    ai_deinit();
    pose_rgn_deinit();
    yolo_rgn_deinit();
ai_init_failed:
    sample_vio_stop_vpss(vpss_grp[0]);
start_vpss_failed:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], 0);
    sample_comm_vi_stop_vi(&vi_cfg);
start_vi_failed:
    sample_comm_sys_exit();
    return ret;
}

static td_s32 sample_vio_ai_pipe0_mode1(const char *om_path, td_s32 bus_id)
{
    td_s32 ret;
    td_s32 bus_try[2];
    td_u32 i;

    bus_try[0] = bus_id;
    bus_try[1] = (bus_id == 6) ? 7 : 6;

    for (i = 0; i < 2; i++) {
        printf("\n==== bus try %u/2: bus_id=%d ====\n", i + 1, bus_try[i]);
        ret = sample_vio_ai_run_once(om_path, bus_try[i]);
        if (ret == TD_SUCCESS) {
            return TD_SUCCESS;
        }
    }

    return TD_FAILURE;
}

static td_s32 sample_vio_ai_attach_mode(const char *om_path, ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_s32 ret;
    td_bool depth_changed = TD_FALSE;
    td_u32 old_depth = 0;
    td_bool req_ok = TD_FALSE;
    td_bool req_depth_changed = TD_FALSE;
    td_u32 req_old_depth = 0;
    ot_vpss_grp run_grp = grp;
    ot_vpss_chn run_chn = chn;
    td_bool found = TD_FALSE;
    int g;
    int c;
    int pass;
    ot_vpss_chn_attr chn_attr;

    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));

    g_attach_pipeline_mode = TD_TRUE;
    printf("attach mode: grp=%d chn=%d (reuse existing vio pipeline)\n", grp, chn);
    if (sample_vio_ai_wait_vpss_chn_ready(grp, chn, 45U) != TD_TRUE) {
        printf("attach mode: vpss chn not ready grp=%d chn=%d within 45s\n", grp, chn);
        return TD_FAILURE;
    }
    if (sample_vio_ai_try_attach_target(grp, chn, &depth_changed, &old_depth) == TD_TRUE) {
        req_ok = TD_TRUE;
        req_depth_changed = depth_changed;
        req_old_depth = old_depth;
        found = TD_TRUE;
        run_grp = grp;
        run_chn = chn;
        printf("attach mode: use requested target grp=%d chn=%d\n", run_grp, run_chn);
        if (ss_mpi_vpss_get_chn_attr(run_grp, run_chn, &chn_attr) == TD_SUCCESS) {
            td_u32 area = chn_attr.width * chn_attr.height;
            if (area > (1920U * 1080U)) {
                printf("attach mode: requested target is %ux%u (>1080p), try preferred <=1080p target first\n",
                    chn_attr.width, chn_attr.height);
                found = TD_FALSE;
            }
        }
    } else {
        printf("attach mode: requested target has no frame, auto probe fallback...\n");
    }
    if (found != TD_TRUE) {
            const char *ai_mode = getenv("WIDGET_AI_MODE");
            td_bool cls_mode = (ai_mode != TD_NULL) && (strcmp(ai_mode, "cls") == 0);
            for (pass = 0; pass < 3; pass++) {
                /* camera_pipe 固定 grp0 + phys ch0..3；勿扫 ch4+（无效且会干扰 MPP） */
                for (g = 0; g < 1; g++) {
                    for (c = 0; c < 4; c++) {
                    td_bool dchg = TD_FALSE;
                    td_u32 dold = 0;
                    td_bool prefer_1080p = TD_FALSE;
                    td_bool prefer_640 = TD_FALSE;
                    td_bool prefer_224 = TD_FALSE;
                    /* If requested target already works, probe others first then fallback to it. */
                    if (req_ok == TD_TRUE && (g == grp) && (c == chn)) {
                        continue;
                    }
                    if (ss_mpi_vpss_get_chn_attr((ot_vpss_grp)g, (ot_vpss_chn)c, &chn_attr) != TD_SUCCESS) {
                        continue;
                    }
                    if ((chn_attr.width <= 1920U) && (chn_attr.height <= 1080U) &&
                        (chn_attr.width >= 640U) && (chn_attr.height >= 360U)) {
                        prefer_1080p = TD_TRUE;
                    }
                    if ((chn_attr.width == 640U) && (chn_attr.height == 640U)) {
                        prefer_640 = TD_TRUE;
                    }
                    if ((chn_attr.width == 224U) && (chn_attr.height == 224U)) {
                        prefer_224 = TD_TRUE;
                    }
                    if (cls_mode != TD_FALSE) {
                        if ((pass == 0) && (prefer_224 != TD_TRUE)) {
                            continue;
                        }
                        if ((pass == 1) && ((prefer_640 != TD_TRUE) || (prefer_224 == TD_TRUE))) {
                            continue;
                        }
                        if ((pass == 2) && ((prefer_1080p != TD_TRUE) || (prefer_640 == TD_TRUE) || (prefer_224 == TD_TRUE))) {
                            continue;
                        }
                    } else {
                        if ((pass == 0) && (prefer_640 != TD_TRUE)) {
                            continue;
                        }
                        if ((pass == 1) && ((prefer_1080p != TD_TRUE) || (prefer_640 == TD_TRUE))) {
                            continue;
                        }
                        if ((pass == 2) && (prefer_1080p == TD_TRUE)) {
                            continue;
                        }
                    }
                    if (sample_vio_ai_try_attach_target((ot_vpss_grp)g, (ot_vpss_chn)c, &dchg, &dold) == TD_TRUE) {
                        found = TD_TRUE;
                        run_grp = (ot_vpss_grp)g;
                        run_chn = (ot_vpss_chn)c;
                        depth_changed = dchg;
                        old_depth = dold;
                        printf("attach mode: fallback target found grp=%d chn=%d size=%ux%u pass=%d\n",
                            run_grp, run_chn, chn_attr.width, chn_attr.height, pass);
                        break;
                    }
                }
                if (found == TD_TRUE) {
                    break;
                }
            }
            if (found == TD_TRUE) {
                break;
            }
        }
    }
    if ((found != TD_TRUE) && (req_ok == TD_TRUE)) {
        found = TD_TRUE;
        run_grp = grp;
        run_chn = chn;
        depth_changed = req_depth_changed;
        old_depth = req_old_depth;
        printf("attach mode: fallback to requested target grp=%d chn=%d\n", run_grp, run_chn);
    }
    if (found != TD_TRUE) {
        printf("attach mode: no usable vpss target found, stop (self-built pipeline disabled)\n");
        return TD_FAILURE;
    }
    g_attach_grp = run_grp;
    g_attach_chn = run_chn;
    yolo_tune_init_once();
    yolo_track_reset();
    yolo_refresh_preview_src_size();
    printf("attach preview src=%ux%u ai_chn=%d\n", g_preview_src_w, g_preview_src_h, run_chn);

    ret = ai_init(om_path);
    if (ret != TD_SUCCESS) {
        printf("ai_init failed ret=0x%x\n", (td_u32)ret);
        if (depth_changed == TD_TRUE) {
            (td_void)ss_mpi_vpss_get_chn_attr(run_grp, run_chn, &chn_attr);
            chn_attr.depth = old_depth;
            (td_void)ss_mpi_vpss_set_chn_attr(run_grp, run_chn, &chn_attr);
        }
        return ret;
    }

    yolo_display_thread_start();
    vio_ai_loop(run_grp, run_chn);
    yolo_display_thread_stop();
    hit_replay_worker_stop();
    ai_deinit();
    pose_rgn_deinit();
    yolo_rgn_deinit();

    if (depth_changed == TD_TRUE) {
        if (ss_mpi_vpss_get_chn_attr(run_grp, run_chn, &chn_attr) == TD_SUCCESS) {
            chn_attr.depth = old_depth;
            ret = ss_mpi_vpss_set_chn_attr(run_grp, run_chn, &chn_attr);
            printf("attach mode: restore vpss depth grp=%d chn=%d to %u ret=0x%x\n",
                run_grp, run_chn, old_depth, (td_u32)ret);
        }
    }

    return TD_SUCCESS;
}

static td_s32 sample_vio_ai_offline_mode(const char *om_path)
{
    td_s32 ret;

    printf("offline mode: model=%s\n", om_path);
    ret = ai_init(om_path);
    if (ret != TD_SUCCESS) {
        printf("offline: ai_init failed ret=0x%x\n", (td_u32)ret);
        return ret;
    }

    ret = ai_infer_offline_once();
    ai_deinit();
    return ret;
}

#ifdef __LITEOS__
td_s32 app_main(td_s32 argc, td_char *argv[])
#else
td_s32 main(td_s32 argc, td_char *argv[])
#endif
{
    const char *om_path = (argc >= 2) ? argv[1] : "/opt/best3.om";
    td_s32 bus_id = 6;
    td_bool attach_mode = TD_FALSE;
    td_bool offline_mode = TD_FALSE;
    ot_vpss_grp attach_grp = 0;
    ot_vpss_chn attach_chn = 0;
    /* use sample_vio's signal handler & flag */
#ifndef __LITEOS__
    sample_register_sig_handler(sample_vio_handle_sig);
#endif
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)setvbuf(stderr, NULL, _IOLBF, 0);

    if ((argc >= 3) && (strcmp(argv[2], "attach") == 0)) {
        attach_mode = TD_TRUE;
        if (argc >= 4) {
            attach_grp = (ot_vpss_grp)atoi(argv[3]);
        }
        if (argc >= 5) {
            attach_chn = (ot_vpss_chn)atoi(argv[4]);
        }
    } else if ((argc >= 3) && (strcmp(argv[2], "offline") == 0)) {
        offline_mode = TD_TRUE;
    } else if (argc >= 3) {
        bus_id = atoi(argv[2]);
    }

    if (offline_mode == TD_TRUE) {
        return sample_vio_ai_offline_mode(om_path);
    }
    if (attach_mode == TD_TRUE) {
        return sample_vio_ai_attach_mode(om_path, attach_grp, attach_chn);
    }
    return sample_vio_ai_pipe0_mode1(om_path, bus_id);
}

