#include "vio_ai_internal.h"

#define POSE_LINE_COLOR       0x00ff00U
#define RGN_RGB(r, g, b) (((td_u32)(r) << 16) | ((td_u32)(g) << 8) | (td_u32)(b))
#define RGN_FROM_BGR(b, g, r) RGN_RGB(r, g, b)

static const td_u32 g_pose_palette[20] = {
    RGN_FROM_BGR(0, 128, 255), RGN_FROM_BGR(51, 153, 255), RGN_FROM_BGR(102, 178, 255),
    RGN_FROM_BGR(0, 230, 230), RGN_FROM_BGR(255, 153, 255), RGN_FROM_BGR(255, 204, 153),
    RGN_FROM_BGR(255, 102, 255), RGN_FROM_BGR(255, 51, 255), RGN_FROM_BGR(255, 178, 102),
    RGN_FROM_BGR(255, 153, 51), RGN_FROM_BGR(153, 153, 255), RGN_FROM_BGR(102, 102, 255),
    RGN_FROM_BGR(51, 51, 255), RGN_FROM_BGR(153, 255, 153), RGN_FROM_BGR(102, 255, 102),
    RGN_FROM_BGR(51, 255, 51), RGN_FROM_BGR(0, 255, 0), RGN_FROM_BGR(255, 0, 0),
    RGN_FROM_BGR(0, 0, 255), RGN_FROM_BGR(255, 255, 255)
};

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

static td_u32 overlay_effective_line_width(td_u32 map_w, td_u32 map_h, td_u32 env_thick)
{
    if (g_pose_line_auto != 0) {
        return overlay_auto_line_width(map_w, map_h);
    }
    return env_thick;
}

static td_u32 pose_limb_color(td_u32 edge_idx)
{
    (void)edge_idx;
    return POSE_LINE_COLOR;
}

/* YOLOv8 pose skeleton (COCO 17 keypoints) */
static const td_u8 g_pose_skeleton[POSE_SKELETON_EDGES][2] = {
    {15, 13}, {13, 11}, {16, 14}, {14, 12}, {11, 12},
    {5, 11}, {6, 12}, {5, 6}, {5, 7}, {6, 8},
    {7, 9}, {8, 10}, {1, 2}, {0, 1}, {0, 2},
    {1, 3}, {2, 4}, {3, 5}, {4, 6}
};

static td_void pose_rgn_hide_all_edges(td_void)
{
    int i;

    if (g_pose_rgn_inited != TD_TRUE) {
        return;
    }
    for (i = 0; i < POSE_SKELETON_EDGES; i++) {
        ot_rgn_handle h = POSE_RGN_LINE_BASE + i;
        ot_rgn_chn_attr chn_attr;
        ot_rgn_line_chn_attr *line;

        (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
        chn_attr.type = OT_RGN_LINE;
        chn_attr.is_show = TD_FALSE;
        line = &chn_attr.attr.line_chn;
        line->thick = 2U;
        line->color = POSE_LINE_COLOR;
        line->points[0].x = 0;
        line->points[0].y = 0;
        line->points[1].x = 0;
        line->points[1].y = 0;
        (td_void)ss_mpi_rgn_set_display_attr(h, &g_rgn_chn, &chn_attr);
    }
    (td_void)memset_s(g_pose_edge_was_show, sizeof(g_pose_edge_was_show), 0, sizeof(g_pose_edge_was_show));
}

static td_void pose_rgn_hide_bbox(td_void)
{
    ot_rgn_chn_attr chn_attr;
    ot_rgn_corner_rect_chn_attr *cr;

    if (g_pose_bbox_rgn_inited != TD_TRUE) {
        return;
    }
    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
    chn_attr.type = OT_RGN_CORNER_RECTEX;
    chn_attr.is_show = TD_FALSE;
    cr = &chn_attr.attr.corner_rectex_chn;
    cr->layer = 0;
    (td_void)ss_mpi_rgn_set_display_attr(POSE_RGN_BBOX_HANDLE, &g_rgn_chn, &chn_attr);
}

static td_s32 pose_rgn_try_init_bbox(const ot_mpp_chn *chn)
{
    ot_rgn_handle h = POSE_RGN_BBOX_HANDLE;
    ot_rgn_attr attr;
    ot_rgn_chn_attr chn_attr;
    ot_rgn_corner_rect_chn_attr *cr;
    td_s32 ret;

    if (g_pose_box_draw == 0 || chn == TD_NULL) {
        return TD_SUCCESS;
    }
    (td_void)memset_s(&attr, sizeof(attr), 0, sizeof(attr));
    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
    attr.type = OT_RGN_CORNER_RECTEX;
    ret = ss_mpi_rgn_create(h, &attr);
    if ((ret != TD_SUCCESS) && (ret != OT_ERR_RGN_EXIST)) {
        return ret;
    }
    chn_attr.is_show = TD_FALSE;
    chn_attr.type = OT_RGN_CORNER_RECTEX;
    cr = &chn_attr.attr.corner_rectex_chn;
    cr->layer = 0;
    cr->corner_rect.thick = 4;
    cr->corner_rect.hor_len = 16;
    cr->corner_rect.ver_len = 16;
    cr->corner_rect_attr.color = g_pose_palette[0];
    cr->corner_rect_attr.corner_rect_type = OT_CORNER_RECT_TYPE_FULL_LINE;
    ret = ss_mpi_rgn_attach_to_chn(h, chn, &chn_attr);
    if ((ret != TD_SUCCESS) && (ret != OT_ERR_RGN_EXIST)) {
        return ret;
    }
    g_pose_bbox_rgn_inited = TD_TRUE;
    return TD_SUCCESS;
}

static td_void pose_rgn_update_bbox(const pose_result_t *pose, td_u32 map_w, td_u32 map_h,
    td_u32 off_x, td_u32 off_y, float sx, float sy, td_u32 thick, td_u32 bracket_len)
{
    ot_rgn_chn_attr chn_attr;
    ot_rgn_corner_rect_chn_attr *cr;
    int x1;
    int y1;
    int x2;
    int y2;
    int w;
    int hgt;
    td_u32 lim_w;
    td_u32 lim_h;

    if (g_pose_box_draw == 0 || g_pose_bbox_rgn_inited != TD_TRUE) {
        return;
    }
    (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
    chn_attr.type = OT_RGN_CORNER_RECTEX;
    cr = &chn_attr.attr.corner_rectex_chn;
    cr->layer = 0;
    cr->corner_rect.thick = thick;
    cr->corner_rect.hor_len = bracket_len;
    cr->corner_rect.ver_len = bracket_len;
    cr->corner_rect_attr.color = g_pose_palette[0];
    cr->corner_rect_attr.corner_rect_type = OT_CORNER_RECT_TYPE_FULL_LINE;

    if (pose == TD_NULL || pose->valid == 0) {
        chn_attr.is_show = TD_FALSE;
        (td_void)ss_mpi_rgn_set_display_attr(POSE_RGN_BBOX_HANDLE, &g_rgn_chn, &chn_attr);
        return;
    }

    x1 = (int)(pose->x1 * sx + 0.5f) + (int)off_x;
    y1 = (int)(pose->y1 * sy + 0.5f) + (int)off_y;
    x2 = (int)(pose->x2 * sx + 0.5f) + (int)off_x;
    y2 = (int)(pose->y2 * sy + 0.5f) + (int)off_y;
    w = x2 - x1;
    hgt = y2 - y1;
    lim_w = map_w + off_x;
    lim_h = map_h + off_y;
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
    cr->corner_rect.rect.x = x1;
    cr->corner_rect.rect.y = y1;
    cr->corner_rect.rect.width = (td_u32)w;
    cr->corner_rect.rect.height = (td_u32)hgt;
    (td_void)ss_mpi_rgn_set_display_attr(POSE_RGN_BBOX_HANDLE, &g_rgn_chn, &chn_attr);
}

td_void pose_rgn_deinit(td_void)
{
    int i;
    if (g_pose_rgn_inited != TD_TRUE && g_pose_bbox_rgn_inited != TD_TRUE) {
        return;
    }
    for (i = 0; i < POSE_SKELETON_EDGES; i++) {
        ot_rgn_handle h = POSE_RGN_LINE_BASE + i;
        (td_void)ss_mpi_rgn_detach_from_chn(h, &g_rgn_chn);
        (td_void)ss_mpi_rgn_destroy(h);
    }
    if (g_pose_bbox_rgn_inited == TD_TRUE) {
        (td_void)ss_mpi_rgn_detach_from_chn(POSE_RGN_BBOX_HANDLE, &g_rgn_chn);
        (td_void)ss_mpi_rgn_destroy(POSE_RGN_BBOX_HANDLE);
        g_pose_bbox_rgn_inited = TD_FALSE;
    }
    g_pose_rgn_inited = TD_FALSE;
}

static td_s32 pose_rgn_try_init(const ot_mpp_chn *chn)
{
    int i;
    td_s32 ret;

    if (chn == TD_NULL) {
        return TD_FAILURE;
    }
    for (i = 0; i < POSE_SKELETON_EDGES; i++) {
        ot_rgn_handle h = POSE_RGN_LINE_BASE + i;
        ot_rgn_attr attr;
        ot_rgn_chn_attr chn_attr;
        ot_rgn_line_chn_attr *line;

        (td_void)memset_s(&attr, sizeof(attr), 0, sizeof(attr));
        (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
        attr.type = OT_RGN_LINE;
        ret = ss_mpi_rgn_create(h, &attr);
        if ((ret != TD_SUCCESS) && (ret != OT_ERR_RGN_EXIST)) {
            return ret;
        }
        chn_attr.is_show = TD_FALSE;
        chn_attr.type = OT_RGN_LINE;
        line = &chn_attr.attr.line_chn;
        line->thick = overlay_effective_line_width(g_rgn_disp_w, g_rgn_disp_h, g_pose_line_thick);
        line->color = pose_limb_color((td_u32)i);
        line->points[0].x = 0;
        line->points[0].y = 0;
        line->points[1].x = 8;
        line->points[1].y = 8;
        ret = ss_mpi_rgn_attach_to_chn(h, chn, &chn_attr);
        if ((ret != TD_SUCCESS) && (ret != OT_ERR_RGN_EXIST)) {
            return ret;
        }
    }
    g_pose_rgn_inited = TD_TRUE;
    return TD_SUCCESS;
}

static td_void pose_rgn_lazy_init(td_void)
{
    td_s32 ret;

    if (g_pose_rgn_enable == 0) {
        return;
    }
    if (g_pose_rgn_inited == TD_TRUE) {
        return;
    }
    yolo_rgn_lazy_init();
    if (g_rgn_chn_ready != TD_TRUE) {
        return;
    }
    ret = pose_rgn_try_init(&g_rgn_chn);
    if (ret == TD_SUCCESS) {
        (td_void)pose_rgn_try_init_bbox(&g_rgn_chn);
        printf("pose rgn: attach %d skeleton lines (handle %d..%d)%s\n",
            POSE_SKELETON_EDGES, POSE_RGN_LINE_BASE, POSE_RGN_LINE_BASE + POSE_SKELETON_EDGES - 1,
            (g_pose_box_draw != 0) ? " + person bbox" : "");
    } else {
        printf("pose rgn: init failed ret=0x%x\n", (td_u32)ret);
    }
}

static td_void pose_kpt_update_vis_state(const pose_result_t *pose, float vis_base)
{
    int i;
    float vis_on;
    float vis_off;

    vis_on = vis_base + 0.06f;
    vis_off = vis_base - 0.04f;
    if (vis_off < 0.05f) {
        vis_off = 0.05f;
    }

    for (i = 0; i < POSE_NUM_KEYPOINTS; i++) {
        if (pose == TD_NULL || pose->valid == 0) {
            g_pose_kpt_show[i] = TD_FALSE;
            continue;
        }
        if (pose->kpts[i].v >= vis_on) {
            g_pose_kpt_show[i] = TD_TRUE;
        } else if (pose->kpts[i].v <= vis_off) {
            g_pose_kpt_show[i] = TD_FALSE;
        }
    }
}

static td_void pose_rgn_update_skeleton(const pose_result_t *pose, td_u32 img_w, td_u32 img_h)
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
    td_u32 line_thick;
    td_u32 bracket_len;
    float kpt_vis = vio_ai_env_get_float_default("WIDGET_POSE_KPT_VIS", 0.25f);

    if (g_pose_rgn_enable == 0) {
        return;
    }
    if (g_pose_rgn_inited != TD_TRUE) {
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

    net_w = (img_w > 0) ? img_w : 640U;
    net_h = (img_h > 0) ? img_h : 640U;
    sx = (float)map_w / (float)net_w;
    sy = (float)map_h / (float)net_h;
    line_thick = overlay_effective_line_width(map_w, map_h, g_pose_line_thick);
    bracket_len = overlay_corner_bracket_len(map_w, map_h, line_thick);

    pose_kpt_update_vis_state(pose, kpt_vis);

    for (i = 0; i < POSE_SKELETON_EDGES; i++) {
        ot_rgn_handle h = POSE_RGN_LINE_BASE + i;
        ot_rgn_chn_attr chn_attr;
        ot_rgn_line_chn_attr *line;
        td_u8 k0;
        td_u8 k1;
        int x0;
        int y0;
        int x1;
        int y1;

        (td_void)memset_s(&chn_attr, sizeof(chn_attr), 0, sizeof(chn_attr));
        chn_attr.type = OT_RGN_LINE;
        line = &chn_attr.attr.line_chn;
        line->thick = line_thick;
        line->color = POSE_LINE_COLOR;

        if (pose != TD_NULL && pose->valid != 0) {
            k0 = g_pose_skeleton[i][0];
            k1 = g_pose_skeleton[i][1];
            /* 两端关键点均可见（带滞后），静止时不因置信度抖动而闪烁 */
            if (k0 < POSE_NUM_KEYPOINTS && k1 < POSE_NUM_KEYPOINTS &&
                g_pose_kpt_show[k0] != TD_FALSE && g_pose_kpt_show[k1] != TD_FALSE) {
                x0 = (int)(pose->kpts[k0].x * sx + 0.5f) + (int)off_x;
                y0 = (int)(pose->kpts[k0].y * sy + 0.5f) + (int)off_y;
                x1 = (int)(pose->kpts[k1].x * sx + 0.5f) + (int)off_x;
                y1 = (int)(pose->kpts[k1].y * sy + 0.5f) + (int)off_y;
                if (x0 < (int)off_x) {
                    x0 = (int)off_x;
                }
                if (y0 < (int)off_y) {
                    y0 = (int)off_y;
                }
                if (x1 < (int)off_x) {
                    x1 = (int)off_x;
                }
                if (y1 < (int)off_y) {
                    y1 = (int)off_y;
                }
                chn_attr.is_show = TD_TRUE;
                line->points[0].x = (td_u32)x0;
                line->points[0].y = (td_u32)y0;
                line->points[1].x = (td_u32)x1;
                line->points[1].y = (td_u32)y1;
                g_pose_edge_was_show[i] = TD_TRUE;
            } else {
                if (g_pose_edge_was_show[i] == TD_TRUE) {
                    chn_attr.is_show = TD_FALSE;
                    line->points[0].x = 0;
                    line->points[0].y = 0;
                    line->points[1].x = 0;
                    line->points[1].y = 0;
                    g_pose_edge_was_show[i] = TD_FALSE;
                } else {
                    continue;
                }
            }
        } else {
            if (g_pose_edge_was_show[i] == TD_TRUE) {
                chn_attr.is_show = TD_FALSE;
                line->points[0].x = 0;
                line->points[0].y = 0;
                line->points[1].x = 0;
                line->points[1].y = 0;
                g_pose_edge_was_show[i] = TD_FALSE;
            } else {
                continue;
            }
        }
        (td_void)ss_mpi_rgn_set_display_attr(h, &g_rgn_chn, &chn_attr);
    }

    if (g_pose_box_draw != 0) {
        if (g_pose_bbox_rgn_inited != TD_TRUE) {
            (td_void)pose_rgn_try_init_bbox(&g_rgn_chn);
        }
        pose_rgn_update_bbox(pose, map_w, map_h, off_x, off_y, sx, sy, line_thick, bracket_len);
    }
}

td_void pose_rgn_redraw_cached(td_void)
{
    if (g_pose_rgn_enable == 0 || g_pose_enabled == 0 || g_pose_result.valid == 0) {
        return;
    }
    pose_rgn_lazy_init();
    if (g_pose_rgn_inited != TD_TRUE) {
        return;
    }
    pose_rgn_update_skeleton(&g_pose_result, g_pose_det_w, g_pose_det_h);
}

td_void pose_rgn_clear_now(td_void)
{
    if (g_pose_enabled == 0) {
        return;
    }
    g_pose_miss_streak = 0;
    (td_void)memset_s(&g_pose_result, sizeof(g_pose_result), 0, sizeof(g_pose_result));
    (td_void)memset_s(g_pose_kpt_show, sizeof(g_pose_kpt_show), 0, sizeof(g_pose_kpt_show));
    g_pose_valid_ts_set = TD_FALSE;
    if (g_pose_rgn_enable == 0) {
        return;
    }
    if (g_pose_rgn_inited == TD_TRUE) {
        pose_rgn_hide_all_edges();
        if (g_pose_box_draw != 0) {
            pose_rgn_hide_bbox();
        }
    }
}

td_void pose_rgn_expire_if_stale(td_void)
{
    struct timeval now;
    td_u32 age_ms;

    if (g_pose_rgn_enable == 0 || g_pose_enabled == 0 || g_pose_rgn_inited != TD_TRUE) {
        return;
    }
    if (g_pose_result.valid == 0 || g_pose_valid_ts_set == TD_FALSE) {
        return;
    }
    if (gettimeofday(&now, TD_NULL) != 0) {
        return;
    }
    if (now.tv_sec > g_pose_valid_tv.tv_sec) {
        age_ms = (td_u32)((now.tv_sec - g_pose_valid_tv.tv_sec) * 1000 +
            (now.tv_usec - g_pose_valid_tv.tv_usec) / 1000);
    } else {
        age_ms = (td_u32)((now.tv_usec - g_pose_valid_tv.tv_usec) / 1000);
    }
    if (age_ms >= g_pose_hold_ms) {
        pose_rgn_clear_now();
    }
}
td_void pose_stamp_on_replay_nv12_ex(unsigned char *nv12, td_u32 dst_w, td_u32 dst_h, td_u32 stride,
    const pose_result_t *pose_in)
{
    pose_result_t pose;
    float sx;
    float sy;
    float kpt_vis;
    int i;

    if (nv12 == TD_NULL || pose_in == TD_NULL || pose_in->valid == 0 ||
        g_pose_det_w == 0U || g_pose_det_h == 0U) {
        return;
    }
    pose = *pose_in;
    sx = (float)dst_w / (float)g_pose_det_w;
    sy = (float)dst_h / (float)g_pose_det_h;
    kpt_vis = vio_ai_env_get_float_default("WIDGET_POSE_KPT_VIS", 0.25f);
    for (i = 0; i < POSE_SKELETON_EDGES; i++) {
        td_u8 k0 = g_pose_skeleton[i][0];
        td_u8 k1 = g_pose_skeleton[i][1];
        int x0;
        int y0;
        int x1;
        int y1;

        if (k0 >= POSE_NUM_KEYPOINTS || k1 >= POSE_NUM_KEYPOINTS) {
            continue;
        }
        if (pose.kpts[k0].v < kpt_vis || pose.kpts[k1].v < kpt_vis) {
            continue;
        }
        x0 = (int)(pose.kpts[k0].x * sx + 0.5f);
        y0 = (int)(pose.kpts[k0].y * sy + 0.5f);
        x1 = (int)(pose.kpts[k1].x * sx + 0.5f);
        y1 = (int)(pose.kpts[k1].y * sy + 0.5f);
        vio_ai_draw_line_y_plane(nv12, stride, dst_w, dst_h, x0, y0, x1, y1, 180, 2);
    }
}

static td_void pose_stamp_on_replay_nv12(unsigned char *nv12, td_u32 dst_w, td_u32 dst_h, td_u32 stride)
{
    if (g_pose_enabled == 0 || g_pose_result.valid == 0) {
        return;
    }
    pose_stamp_on_replay_nv12_ex(nv12, dst_w, dst_h, stride, &g_pose_result);
}
static td_void pose_decode_keypoints_for_anchor(const float *out_data, int anchor, int net_w, int net_h,
    int img_w, int img_h, pose_result_t *out)
{
    int k;
    float sx;
    float sy;
    const int anchors = POSE_ANCHOR_NUM;

    sx = (float)img_w / (float)net_w;
    sy = (float)img_h / (float)net_h;
    for (k = 0; k < POSE_NUM_KEYPOINTS; k++) {
        float kx = out_data[(5 + k * 3 + 0) * anchors + anchor];
        float ky = out_data[(5 + k * 3 + 1) * anchors + anchor];
        float kv = out_data[(5 + k * 3 + 2) * anchors + anchor];
        if (kx <= 2.0f && ky <= 2.0f) {
            kx *= (float)net_w;
            ky *= (float)net_h;
        }
        out->kpts[k].x = kx * sx;
        out->kpts[k].y = ky * sy;
        out->kpts[k].v = kv;
    }
}

static int pose_decode_best(const float *out_data, size_t out_float_num, int net_w, int net_h,
    int img_w, int img_h, pose_result_t *best_out)
{
    const int anchors = POSE_ANCHOR_NUM;
    float conf_thres = vio_ai_env_get_float_default("WIDGET_POSE_CONF", 0.10f);
    float iou_thres = vio_ai_env_get_float_default("WIDGET_POSE_IOU", 0.45f);
    yolo_det_t raw[128];
    yolo_det_t nms[8];
    int raw_cnt = 0;
    int nms_cnt;
    int a;
    int best_i = 0;
    int anchor_idx = -1;

    (void)out_float_num;
    if (best_out == TD_NULL) {
        return 0;
    }
    (td_void)memset_s(best_out, sizeof(*best_out), 0, sizeof(*best_out));

    if (conf_thres < 0.05f) {
        conf_thres = 0.05f;
    }
    if (iou_thres < 0.05f) {
        iou_thres = 0.45f;
    }

    for (a = 0; a < anchors && raw_cnt < (int)(sizeof(raw) / sizeof(raw[0])); a++) {
        float cx = out_data[0 * anchors + a];
        float cy = out_data[1 * anchors + a];
        float w = out_data[2 * anchors + a];
        float h = out_data[3 * anchors + a];
        float conf = yolo_prob(out_data[4 * anchors + a]);
        float x1;
        float y1;
        float x2;
        float y2;

        if (conf < conf_thres) {
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
        raw[raw_cnt].x1 = x1;
        raw[raw_cnt].y1 = y1;
        raw[raw_cnt].x2 = x2;
        raw[raw_cnt].y2 = y2;
        raw[raw_cnt].score = conf;
        raw[raw_cnt].cls_id = a;
        raw_cnt++;
    }

    nms_cnt = yolo_nms(raw, raw_cnt, nms, 1, iou_thres);
    if (nms_cnt <= 0) {
        return 0;
    }

    best_i = 0;
    for (a = 1; a < nms_cnt; a++) {
        if (nms[a].score > nms[best_i].score) {
            best_i = a;
        }
    }
    anchor_idx = nms[best_i].cls_id;
    best_out->x1 = nms[best_i].x1;
    best_out->y1 = nms[best_i].y1;
    best_out->x2 = nms[best_i].x2;
    best_out->y2 = nms[best_i].y2;
    best_out->score = nms[best_i].score;
    pose_decode_keypoints_for_anchor(out_data, anchor_idx, net_w, net_h, img_w, img_h, best_out);
    best_out->valid = 1;
    return 1;
}

static float pose_kpt_motion_px(const pose_result_t *prev, const pose_result_t *next)
{
    int i;
    int n = 0;
    float sum = 0.0f;

    if (prev == TD_NULL || next == TD_NULL || prev->valid == 0) {
        return 999.0f;
    }
    for (i = 0; i < POSE_NUM_KEYPOINTS; i++) {
        float dx;
        float dy;
        if (prev->kpts[i].v < 0.20f && next->kpts[i].v < 0.20f) {
            continue;
        }
        dx = next->kpts[i].x - prev->kpts[i].x;
        dy = next->kpts[i].y - prev->kpts[i].y;
        sum += sqrtf(dx * dx + dy * dy);
        n++;
    }
    return (n > 0) ? (sum / (float)n) : 999.0f;
}

static float pose_bbox_center_shift_px(const pose_result_t *prev, const pose_result_t *next)
{
    float pcx;
    float pcy;
    float ncx;
    float ncy;

    if (prev == TD_NULL || next == TD_NULL || prev->valid == 0) {
        return 999.0f;
    }
    pcx = (prev->x1 + prev->x2) * 0.5f;
    pcy = (prev->y1 + prev->y2) * 0.5f;
    ncx = (next->x1 + next->x2) * 0.5f;
    ncy = (next->y1 + next->y2) * 0.5f;
    return sqrtf((ncx - pcx) * (ncx - pcx) + (ncy - pcy) * (ncy - pcy));
}

static void pose_blend_keypoints(pose_result_t *out, const pose_result_t *prev, const pose_result_t *next, float alpha)
{
    int i;
    float beta;

    if (out == TD_NULL || prev == TD_NULL || next == TD_NULL) {
        return;
    }
    if (alpha < 0.10f) {
        alpha = 0.10f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    beta = 1.0f - alpha;
    *out = *next;
    for (i = 0; i < POSE_NUM_KEYPOINTS; i++) {
        if (prev->kpts[i].v >= 0.20f && next->kpts[i].v >= 0.20f) {
            out->kpts[i].x = alpha * next->kpts[i].x + beta * prev->kpts[i].x;
            out->kpts[i].y = alpha * next->kpts[i].y + beta * prev->kpts[i].y;
            out->kpts[i].v = (next->kpts[i].v > prev->kpts[i].v) ? next->kpts[i].v : prev->kpts[i].v;
        }
    }
}

static void pose_apply_stabilized(pose_result_t *out, const pose_result_t *prev, const pose_result_t *meas, float motion)
{
    int i;
    float alpha;
    float beta;
    float snap_px;

    if (out == TD_NULL || prev == TD_NULL || meas == TD_NULL) {
        return;
    }
    if (prev->valid == 0) {
        *out = *meas;
        return;
    }

    *out = *meas;
    snap_px = g_pose_kpt_snap_px;

    if (motion >= g_pose_motion_px) {
        return;
    }

    alpha = g_pose_smooth_alpha;
    if (motion < g_pose_stable_motion_px) {
        alpha = g_pose_smooth_alpha * 0.40f;
        if (alpha < 0.12f) {
            alpha = 0.12f;
        }
    }
    beta = 1.0f - alpha;

    for (i = 0; i < POSE_NUM_KEYPOINTS; i++) {
        float dx;
        float dy;
        float dist;

        if (prev->kpts[i].v >= 0.15f && meas->kpts[i].v < 0.12f) {
            out->kpts[i] = prev->kpts[i];
            continue;
        }
        if (prev->kpts[i].v < 0.15f || meas->kpts[i].v < 0.12f) {
            continue;
        }

        dx = meas->kpts[i].x - prev->kpts[i].x;
        dy = meas->kpts[i].y - prev->kpts[i].y;
        dist = sqrtf(dx * dx + dy * dy);
        if (dist <= snap_px) {
            out->kpts[i].x = prev->kpts[i].x;
            out->kpts[i].y = prev->kpts[i].y;
            out->kpts[i].v = (meas->kpts[i].v > prev->kpts[i].v) ? meas->kpts[i].v : prev->kpts[i].v;
        } else {
            out->kpts[i].x = alpha * meas->kpts[i].x + beta * prev->kpts[i].x;
            out->kpts[i].y = alpha * meas->kpts[i].y + beta * prev->kpts[i].y;
            out->kpts[i].v = (meas->kpts[i].v > prev->kpts[i].v) ? meas->kpts[i].v : prev->kpts[i].v;
        }
    }

    if (motion < g_pose_stable_motion_px) {
        out->x1 = alpha * meas->x1 + beta * prev->x1;
        out->y1 = alpha * meas->y1 + beta * prev->y1;
        out->x2 = alpha * meas->x2 + beta * prev->x2;
        out->y2 = alpha * meas->y2 + beta * prev->y2;
    }
}

void pose_postprocess_and_draw(const float *out_data, size_t out_float_num, td_u32 img_w, td_u32 img_h)
{
    td_u32 net_w = 640U;
    td_u32 net_h = 640U;
    td_u32 det_w;
    td_u32 det_h;
    pose_result_t pose;
    pose_result_t prev;

    det_w = (img_w > 0) ? img_w : net_w;
    det_h = (img_h > 0) ? img_h : net_h;
    g_pose_det_w = det_w;
    g_pose_det_h = det_h;
    prev = g_pose_result;
    (td_void)memset_s(&pose, sizeof(pose), 0, sizeof(pose));

    if (pose_decode_best(out_data, out_float_num, (int)net_w, (int)net_h,
            (int)det_w, (int)det_h, &pose) != 0) {
        float motion = pose_kpt_motion_px(&prev, &pose);
        float bbox_shift = pose_bbox_center_shift_px(&prev, &pose);

        g_pose_miss_streak = 0;
        if (gettimeofday(&g_pose_valid_tv, TD_NULL) == 0) {
            g_pose_valid_ts_set = TD_TRUE;
        }
        if (prev.valid != 0 && bbox_shift >= g_pose_bbox_jump_px) {
            if (g_pose_rgn_enable != 0) {
                pose_rgn_hide_all_edges();
            }
            g_pose_result = pose;
        } else {
            pose_apply_stabilized(&g_pose_result, &prev, &pose, motion);
        }
    } else {
        g_pose_miss_streak++;
        if (g_pose_miss_streak >= g_pose_miss_max) {
            pose_rgn_clear_now();
        }
    }

    pose_rgn_lazy_init();
    if (g_pose_rgn_enable != 0 && g_pose_result.valid != 0) {
        pose_rgn_update_skeleton(&g_pose_result, det_w, det_h);
    }
}
td_s32 ai_pose_load(const char *path)
{
    td_s32 ret;

    if ((path == TD_NULL) || (path[0] == '\0') || (access(path, R_OK) != 0)) {
        printf("ai_pose: model not found: %s\n", (path != TD_NULL) ? path : "(null)");
        return TD_FAILURE;
    }

    ret = aclmdlLoadFromFile(path, &g_pose_model_id);
    if (ret != ACL_SUCCESS) {
        printf("ai_pose: aclmdlLoadFromFile failed ret=%d path=%s\n", (int)ret, path);
        return TD_FAILURE;
    }

    g_pose_model_desc = aclmdlCreateDesc();
    if (g_pose_model_desc == TD_NULL) {
        (td_void)aclmdlUnload(g_pose_model_id);
        g_pose_model_id = 0;
        return TD_FAILURE;
    }
    ret = aclmdlGetDesc(g_pose_model_desc, g_pose_model_id);
    if (ret != ACL_SUCCESS) {
        aclmdlDestroyDesc(g_pose_model_desc);
        g_pose_model_desc = TD_NULL;
        (td_void)aclmdlUnload(g_pose_model_id);
        g_pose_model_id = 0;
        return TD_FAILURE;
    }

    g_pose_enabled = 1;
    g_pose_vpss_chn = (ot_vpss_chn)vio_ai_env_get_int_default("WIDGET_POSE_CHN",
        vio_ai_env_get_int_default("WIDGET_YOLO_DET_CHN", 2));
    {
        aclmdlIODims idims;
        aclmdlIODims odims;
        td_u32 i;
        (td_void)memset_s(&idims, sizeof(idims), 0, sizeof(idims));
        (td_void)memset_s(&odims, sizeof(odims), 0, sizeof(odims));
        printf("ai_pose: loaded %s vpss_chn=%d pose=640x640\n", path, (int)g_pose_vpss_chn);
        if (aclmdlGetInputDims(g_pose_model_desc, 0, &idims) == ACL_SUCCESS) {
            printf("ai_pose input dims=");
            for (i = 0; i < idims.dimCount; i++) {
                printf("%lld%s", (long long)idims.dims[i], (i + 1 == idims.dimCount) ? "" : "x");
            }
            printf("\n");
        }
        if (aclmdlGetOutputDims(g_pose_model_desc, 0, &odims) == ACL_SUCCESS) {
            printf("ai_pose output dims=");
            for (i = 0; i < odims.dimCount; i++) {
                printf("%lld%s", (long long)odims.dims[i], (i + 1 == odims.dimCount) ? "" : "x");
            }
            printf(" size=%zu\n", aclmdlGetOutputSizeByIndex(g_pose_model_desc, 0));
        }
        if (g_pose_rgn_enable != 0) {
            printf("ai_pose: live skeleton RGN on (WIDGET_POSE_RGN=1)\n");
        } else {
            printf("ai_pose: live RGN off, replay stamp only (WIDGET_POSE_RGN=0)\n");
            pose_rgn_deinit();
        }
    }
    return TD_SUCCESS;
}

td_void ai_pose_unload(td_void)
{
    if (g_pose_model_desc != TD_NULL) {
        aclmdlDestroyDesc(g_pose_model_desc);
        g_pose_model_desc = TD_NULL;
    }
    if (g_pose_model_id != 0) {
        (td_void)aclmdlUnload(g_pose_model_id);
        g_pose_model_id = 0;
    }
    g_pose_enabled = 0;
}

td_s32 ai_pose_infer(const ot_video_frame_info *frame_info)
{
    aclmdlDesc *saved_desc = g_model_desc;
    uint32_t saved_id = g_model_id;
    int saved_post = g_infer_post_mode;
    td_s32 ret;

    if ((g_pose_enabled == 0) || (g_pose_model_desc == TD_NULL) || (frame_info == TD_NULL)) {
        return TD_FAILURE;
    }

    g_model_desc = g_pose_model_desc;
    g_model_id = g_pose_model_id;
    g_infer_post_mode = YOLO_INFER_POST_POSE;
    ret = ai_infer_from_nv12(frame_info);
    g_model_desc = saved_desc;
    g_model_id = saved_id;
    g_infer_post_mode = saved_post;
    return ret;
}
