/*
 * vio_ai_internal.h - shared types and cross-module API for sample_vio_ai
 */
#ifndef VIO_AI_INTERNAL_H
#define VIO_AI_INTERNAL_H

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

#define POSE_RGN_LINE_BASE    80
#define POSE_SKELETON_EDGES   19
#define POSE_RGN_BBOX_HANDLE  (POSE_RGN_LINE_BASE + POSE_SKELETON_EDGES)
#define POSE_NUM_KEYPOINTS    17
#define POSE_FEAT_CHANNELS    56
#define POSE_ANCHOR_NUM       8400

#define YOLO_INFER_POST_AUTO   0
#define YOLO_INFER_POST_CLS    1
#define YOLO_INFER_POST_PERSON 2
#define YOLO_INFER_POST_POSE   3

typedef struct {
    float x;
    float y;
    float v;
} pose_kpt_t;

typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    pose_kpt_t kpts[POSE_NUM_KEYPOINTS];
    int valid;
} pose_result_t;

typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int cls_id;
} yolo_det_t;

int vio_ai_env_get_int_default(const char *key, int defv);
float vio_ai_env_get_float_default(const char *key, float defv);

td_bool vio_ai_pixel_format_is_nv21(td_u32 pixel_format);
void vio_ai_draw_box_y_plane(unsigned char *y, td_u32 stride, td_u32 img_w, td_u32 img_h,
    int x1, int y1, int x2, int y2, unsigned char v, int thick);
void vio_ai_draw_line_y_plane(unsigned char *y, td_u32 stride, td_u32 img_w, td_u32 img_h,
    int x0, int y0, int x1, int y1, unsigned char v, int thick);
td_bool vio_ai_resize_yuv420sp_letterbox(const unsigned char *src, td_u32 src_w, td_u32 src_h,
    td_u32 src_stride_y, td_u32 src_stride_uv, unsigned char *dst, td_u32 dst_w, td_u32 dst_h,
    td_bool src_is_nv21, td_bool dst_is_nv21);
void vio_ai_resize_yuv420sp_bilinear(const unsigned char *src, td_u32 src_w, td_u32 src_h,
    td_u32 src_stride_y, td_u32 src_stride_uv, unsigned char *dst, td_u32 dst_w, td_u32 dst_h,
    td_bool src_is_nv21, td_bool dst_is_nv21);
void resize_yuv420sp_nn(const unsigned char *src, td_u32 src_w, td_u32 src_h, td_u32 src_stride_y, td_u32 src_stride_uv,
    unsigned char *dst, td_u32 dst_w, td_u32 dst_h, td_bool src_is_nv21, td_bool dst_is_nv21);

float yolo_prob(float x);
int yolo_nms(yolo_det_t *src, int src_cnt, yolo_det_t *dst, int max_dst, float iou_thres);
td_u32 overlay_corner_bracket_len(td_u32 map_w, td_u32 map_h, td_u32 thick);
td_void yolo_rgn_refresh_vo_rect(td_void);
td_void yolo_rgn_refresh_vpss_preview_rect(td_void);
td_void yolo_refresh_preview_src_size(td_void);
td_bool hit_replay_uses_dedicated_chn(td_void);
td_void hit_replay_worker_stop(td_void);

void pose_stamp_on_replay_nv12_ex(unsigned char *nv12, td_u32 dst_w, td_u32 dst_h, td_u32 stride,
    const pose_result_t *pose_in);
void pose_postprocess_and_draw(const float *out_data, size_t out_float_num, td_u32 img_w, td_u32 img_h);
td_s32 ai_pose_infer(const ot_video_frame_info *frame_info);
td_s32 ai_pose_load(const char *path);
td_void ai_pose_unload(td_void);
td_void pose_rgn_expire_if_stale(td_void);
td_void pose_rgn_redraw_cached(td_void);
td_void pose_rgn_deinit(td_void);
td_void pose_rgn_clear_now(td_void);

void hit_replay_poll_trigger(td_void);
void hit_replay_poll_pose_trigger(td_void);
td_void hit_replay_submit_frame(const ot_video_frame_info *frame_info);

td_s32 ai_infer_from_nv12(const ot_video_frame_info *frame_info);
td_void yolo_rgn_lazy_init(td_void);

extern pose_result_t g_pose_result;
extern int g_pose_enabled;
extern int g_pose_rgn_enable;
extern ot_vpss_chn g_pose_vpss_chn;
extern td_u32 g_pose_det_w;
extern td_u32 g_pose_det_h;
extern td_u32 g_pose_infer_interval;
extern int g_pose_ch1_only;
extern int g_pose_clear_on_action;
extern int g_pose_clear_on_swing;
extern td_u32 g_pose_line_thick;
extern int g_pose_line_auto;
extern int g_pose_box_draw;
extern td_u32 g_pose_hold_ms;
extern float g_pose_motion_px;
extern float g_pose_stable_motion_px;
extern float g_pose_kpt_snap_px;
extern float g_pose_bbox_jump_px;
extern float g_pose_smooth_alpha;
extern td_u32 g_pose_miss_max;
extern td_u32 g_pose_miss_streak;
extern td_bool g_pose_valid_ts_set;
extern struct timeval g_pose_valid_tv;
extern td_bool g_pose_rgn_inited;
extern td_bool g_pose_bbox_rgn_inited;
extern td_bool g_pose_edge_was_show[POSE_SKELETON_EDGES];
extern td_bool g_pose_kpt_show[POSE_NUM_KEYPOINTS];
extern aclmdlDesc *g_pose_model_desc;
extern uint32_t g_pose_model_id;

extern ot_mpp_chn g_rgn_chn;
extern td_bool g_rgn_chn_ready;
extern td_u32 g_rgn_disp_ox;
extern td_u32 g_rgn_disp_oy;
extern td_u32 g_rgn_disp_w;
extern td_u32 g_rgn_disp_h;
extern td_u32 g_preview_src_w;
extern td_u32 g_preview_src_h;

extern ot_vpss_chn g_attach_chn;
extern ot_vpss_grp g_attach_grp;
extern td_s32 g_replay_src_chn;
extern int g_replay_live_ring;

extern aclmdlDesc *g_model_desc;
extern uint32_t g_model_id;
extern int g_infer_post_mode;

#endif
