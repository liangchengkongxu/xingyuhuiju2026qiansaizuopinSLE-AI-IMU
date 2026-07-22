#include "vio_ai_internal.h"

static td_bool hit_replay_src_is_nv21(td_u32 pixel_format)
{
    td_s32 force = vio_ai_env_get_int_default("WIDGET_REPLAY_SRC_NV21", -1);
    if (force == 0) return TD_FALSE;
    if (force == 1) return TD_TRUE;
    if (g_replay_src_chn < 0) return TD_FALSE;
    if ((ot_vpss_chn)g_replay_src_chn == g_attach_chn) return TD_FALSE;
    if (vio_ai_pixel_format_is_nv21(pixel_format) == TD_TRUE) return TD_TRUE;
#ifdef OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420
    if (pixel_format == (td_u32)OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420) return TD_FALSE;
#endif
    return (g_replay_src_chn >= 0 && (ot_vpss_chn)g_replay_src_chn != g_attach_chn) ? TD_TRUE : TD_FALSE;
}

/* ── 击球回放：环形缓冲 + 前后各 3s 导出 ── */
#define HIT_REPLAY_W 960
#define HIT_REPLAY_H 540
#define HIT_REPLAY_FPS 25
#define HIT_REPLAY_VPSS_CHN_DEFAULT 3
#define HIT_REPLAY_SUBMIT_BYTES ((size_t)1024U * 576U * 3U / 2U)
#define HIT_REPLAY_SEC 2
#define HIT_REPLAY_PRE_FRAMES (HIT_REPLAY_FPS * HIT_REPLAY_SEC)
#define HIT_REPLAY_POST_FRAMES (HIT_REPLAY_FPS * HIT_REPLAY_SEC)
#define HIT_REPLAY_TOTAL_FRAMES (HIT_REPLAY_PRE_FRAMES + HIT_REPLAY_POST_FRAMES)
#define HIT_REPLAY_FRAME_BYTES ((size_t)HIT_REPLAY_W * (size_t)HIT_REPLAY_H * 3 / 2)
#define HIT_REPLAY_REQ_PATH "/tmp/.widget_replay_req"
#define HIT_REPLAY_POSE_REQ_PATH "/tmp/.widget_replay_pose_req"
#define HIT_REPLAY_SESSION_PATH "/tmp/.widget_replay_session"
#define HIT_REPLAY_DEFAULT_DIR "/opt/widget_ui/replays"

typedef struct {
    unsigned char *nv12;
    pose_result_t pose;
    td_bool pose_valid;
} hit_replay_slot_t;

static hit_replay_slot_t g_hit_replay_ring[HIT_REPLAY_PRE_FRAMES];
static unsigned int g_hit_replay_ring_head = 0;
static unsigned int g_hit_replay_ring_count = 0;
static hit_replay_slot_t g_hit_replay_export[HIT_REPLAY_TOTAL_FRAMES];
static unsigned int g_hit_replay_export_count = 0;
static volatile td_bool g_hit_replay_capturing_post = TD_FALSE;
static unsigned int g_hit_replay_post_left = 0;
static char g_hit_replay_session[96] = {0};
static int g_hit_replay_hit_idx = 0;
static struct timeval g_hit_replay_last_push = {0, 0};
static td_bool g_hit_replay_inited = TD_FALSE;
static pthread_mutex_t g_hit_replay_mutex = PTHREAD_MUTEX_INITIALIZER;

#define HIT_REPLAY_PENDING_MAX 16
typedef struct {
    char session[96];
    int hit_idx;
} hit_replay_pending_t;

static hit_replay_pending_t g_hit_replay_pending[HIT_REPLAY_PENDING_MAX];
static unsigned int g_hit_replay_pending_wr = 0;
static unsigned int g_hit_replay_pending_rd = 0;
static td_bool g_hit_replay_session_on = TD_FALSE;
static struct timeval g_hit_replay_session_check = {0, 0};
static td_s32 g_hit_replay_pose_eager_max = 3;

typedef struct {
    unsigned char *nv12;
    td_u32 width;
    td_u32 height;
    td_u32 stride_y;
    td_u32 stride_uv;
    td_bool is_nv21;
    td_bool pending;
    ot_pixel_format pixel_format;
} hit_replay_submit_t;

static hit_replay_submit_t g_replay_submit;
static pthread_mutex_t g_replay_worker_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_replay_worker_cv = PTHREAD_COND_INITIALIZER;
static volatile int g_replay_worker_run = 0;
static pthread_t g_replay_worker_tid;
static td_bool g_replay_worker_on = TD_FALSE;

static td_bool hit_replay_read_session_active(td_void)
{
    FILE *fp;
    char buf[128];
    size_t n;

    fp = fopen(HIT_REPLAY_SESSION_PATH, "re");
    if (fp == TD_NULL) {
        return TD_FALSE;
    }
    if (fgets(buf, (int)sizeof(buf), fp) == TD_NULL) {
        (void)fclose(fp);
        return TD_FALSE;
    }
    (void)fclose(fp);
    n = strlen(buf);
    while ((n > 0U) && ((buf[n - 1U] == '\n') || (buf[n - 1U] == '\r') || (buf[n - 1U] == ' '))) {
        buf[n - 1U] = '\0';
        n--;
    }
    return (n > 0U) ? TD_TRUE : TD_FALSE;
}

static td_bool hit_replay_is_active(td_void)
{
    struct timeval now;

    if (gettimeofday(&now, TD_NULL) != 0) {
        return g_hit_replay_session_on;
    }
    if ((g_hit_replay_session_check.tv_sec == 0) ||
        ((now.tv_sec - g_hit_replay_session_check.tv_sec) >= 2)) {
        g_hit_replay_session_on = hit_replay_read_session_active();
        g_hit_replay_session_check = now;
    }
    return g_hit_replay_session_on;
}

static td_bool hit_replay_needs_frames(td_void)
{
    td_bool session_on;
    td_bool busy;

    session_on = hit_replay_is_active();
    (void)pthread_mutex_lock(&g_hit_replay_mutex);
    busy = (g_hit_replay_capturing_post == TD_TRUE) ||
        (g_hit_replay_pending_rd != g_hit_replay_pending_wr);
    (void)pthread_mutex_unlock(&g_hit_replay_mutex);
    return (session_on == TD_TRUE) || (busy == TD_TRUE);
}

static td_void hit_replay_deinit(td_void)
{
    unsigned int i;

    if (g_hit_replay_inited != TD_TRUE) {
        return;
    }
    if (g_hit_replay_capturing_post == TD_TRUE) {
        return;
    }
    for (i = 0; i < HIT_REPLAY_PRE_FRAMES; i++) {
        if (g_hit_replay_ring[i].nv12 != TD_NULL) {
            free(g_hit_replay_ring[i].nv12);
            g_hit_replay_ring[i].nv12 = TD_NULL;
        }
    }
    for (i = 0; i < HIT_REPLAY_TOTAL_FRAMES; i++) {
        if (g_hit_replay_export[i].nv12 != TD_NULL) {
            free(g_hit_replay_export[i].nv12);
            g_hit_replay_export[i].nv12 = TD_NULL;
        }
    }
    g_hit_replay_ring_head = 0;
    g_hit_replay_ring_count = 0;
    g_hit_replay_export_count = 0;
    g_hit_replay_inited = TD_FALSE;
    printf("hit_replay: deinit freed buffers\n");
}

static td_s32 hit_replay_env_int(const char *name, td_s32 def, td_s32 min_v, td_s32 max_v)
{
    const char *v = getenv(name);
    td_s32 n;

    if ((v == TD_NULL) || (v[0] == '\0')) {
        return def;
    }
    n = (td_s32)strtol(v, TD_NULL, 10);
    if (n < min_v || n > max_v) {
        return def;
    }
    return n;
}

static const char *hit_replay_dir_base(td_void)
{
    const char *v = getenv("WIDGET_REPLAY_DIR");
    if ((v != TD_NULL) && (v[0] != '\0')) {
        return v;
    }
    return HIT_REPLAY_DEFAULT_DIR;
}

static td_s32 hit_replay_mkdir_p(const char *path)
{
    char tmp[256];
    char *p = TD_NULL;
    size_t len;

    if ((path == TD_NULL) || (path[0] == '\0')) {
        return TD_FAILURE;
    }
    if (strncpy_s(tmp, sizeof(tmp), path, sizeof(tmp) - 1) != EOK) {
        return TD_FAILURE;
    }
    len = strlen(tmp);
    if ((len == 0) || (len >= sizeof(tmp))) {
        return TD_FAILURE;
    }
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if ((mkdir(tmp, 0755) != 0) && (errno != EEXIST)) {
            return TD_FAILURE;
        }
        *p = '/';
    }
    if ((mkdir(tmp, 0755) != 0) && (errno != EEXIST)) {
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

static td_void hit_replay_init_once(td_void)
{
    if (g_hit_replay_inited == TD_TRUE) {
        return;
    }
    g_hit_replay_inited = TD_TRUE;
    g_hit_replay_pose_eager_max = hit_replay_env_int("WIDGET_REPLAY_POSE_EAGER_MAX", 3, 0, 32);
    g_replay_src_chn = hit_replay_env_int("WIDGET_REPLAY_VPSS_CHN", HIT_REPLAY_VPSS_CHN_DEFAULT, -1, 3);
    if (g_replay_live_ring != 0) {
        printf("hit_replay: lazy alloc pre=%u post=%u @%dfps out=%ux%u src_chn=%d pose_eager=%d dir=%s\n",
            (unsigned int)HIT_REPLAY_PRE_FRAMES, (unsigned int)HIT_REPLAY_POST_FRAMES,
            HIT_REPLAY_FPS, (unsigned int)HIT_REPLAY_W, (unsigned int)HIT_REPLAY_H,
            (int)g_replay_src_chn, (int)g_hit_replay_pose_eager_max, hit_replay_dir_base());
    } else {
        printf("hit_replay: post-only mode post=%u pose_eager=%d dir=%s (no live ring)\n",
            (unsigned int)HIT_REPLAY_POST_FRAMES, (int)g_hit_replay_pose_eager_max, hit_replay_dir_base());
    }
}

static td_void hit_replay_snapshot_pose(hit_replay_slot_t *slot)
{
    if (slot == TD_NULL) {
        return;
    }
    if (g_pose_enabled != 0 && g_pose_result.valid != 0) {
        slot->pose = g_pose_result;
        slot->pose_valid = TD_TRUE;
    } else {
        (td_void)memset_s(&slot->pose, sizeof(slot->pose), 0, sizeof(slot->pose));
        slot->pose_valid = TD_FALSE;
    }
}

static td_void hit_replay_copy_slot(hit_replay_slot_t *dst, const hit_replay_slot_t *src)
{
    if ((dst == TD_NULL) || (src == TD_NULL) || (src->nv12 == TD_NULL) || (dst->nv12 == TD_NULL)) {
        return;
    }
    (td_void)memcpy_s(dst->nv12, HIT_REPLAY_FRAME_BYTES, src->nv12, HIT_REPLAY_FRAME_BYTES);
    dst->pose = src->pose;
    dst->pose_valid = src->pose_valid;
}

static unsigned char *hit_replay_ring_slot(unsigned int idx)
{
    if (idx >= HIT_REPLAY_PRE_FRAMES) {
        return TD_NULL;
    }
    if (g_hit_replay_ring[idx].nv12 == TD_NULL) {
        g_hit_replay_ring[idx].nv12 = (unsigned char *)malloc(HIT_REPLAY_FRAME_BYTES);
        if (g_hit_replay_ring[idx].nv12 == TD_NULL) {
            printf("hit_replay: ring slot %u alloc failed\n", idx);
        }
    }
    return g_hit_replay_ring[idx].nv12;
}

static unsigned char *hit_replay_export_slot(unsigned int idx)
{
    if (idx >= HIT_REPLAY_TOTAL_FRAMES) {
        return TD_NULL;
    }
    if (g_hit_replay_export[idx].nv12 == TD_NULL) {
        g_hit_replay_export[idx].nv12 = (unsigned char *)malloc(HIT_REPLAY_FRAME_BYTES);
        if (g_hit_replay_export[idx].nv12 == TD_NULL) {
            printf("hit_replay: export slot %u alloc failed\n", idx);
        }
    }
    return g_hit_replay_export[idx].nv12;
}

static td_u8 hit_replay_u8_clip(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (td_u8)v;
}

static td_void hit_replay_nv12_to_rgb24(const unsigned char *nv12, unsigned char *rgb, td_u32 w, td_u32 h, td_bool is_nv21)
{
    td_u32 y;
    td_u32 x;
    const unsigned char *y_plane = nv12;
    const unsigned char *uv_plane = nv12 + (size_t)w * h;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int yy = (int)y_plane[y * w + x];
            td_u32 uv_idx = (y / 2) * w + (x & ~1U);
            int u_raw;
            int v_raw;
            int c;
            int d;
            int e;
            int r;
            int g;
            int b;

            if (is_nv21 == TD_TRUE) {
                v_raw = (int)uv_plane[uv_idx];
                u_raw = (int)uv_plane[uv_idx + 1];
            } else {
                u_raw = (int)uv_plane[uv_idx];
                v_raw = (int)uv_plane[uv_idx + 1];
            }
            /* VPSS 输出为 BT.601 有限范围 (Y 16~235)，与 yolo_yuv420sp_to_bgr_u8 一致 */
            c = yy - 16;
            if (c < 0) {
                c = 0;
            }
            d = u_raw - 128;
            e = v_raw - 128;
            r = (298 * c + 409 * e + 128) >> 8;
            g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            b = (298 * c + 516 * d + 128) >> 8;
            size_t o = ((size_t)y * w + x) * 3;
            rgb[o + 0] = hit_replay_u8_clip(r);
            rgb[o + 1] = hit_replay_u8_clip(g);
            rgb[o + 2] = hit_replay_u8_clip(b);
        }
    }
}

static td_s32 hit_replay_write_ppm(const char *path, const unsigned char *nv12,
    const pose_result_t *pose, td_bool pose_valid)
{
    FILE *fp = TD_NULL;
    unsigned char *rgb = TD_NULL;
    unsigned char *work = TD_NULL;
    size_t rgb_sz = (size_t)HIT_REPLAY_W * (size_t)HIT_REPLAY_H * 3;

    rgb = (unsigned char *)malloc(rgb_sz);
    if (rgb == TD_NULL) {
        return TD_FAILURE;
    }
    work = (unsigned char *)malloc(HIT_REPLAY_FRAME_BYTES);
    if (work == TD_NULL) {
        free(rgb);
        return TD_FAILURE;
    }
    (td_void)memcpy_s(work, HIT_REPLAY_FRAME_BYTES, nv12, HIT_REPLAY_FRAME_BYTES);
    if (pose_valid == TD_TRUE && pose != TD_NULL) {
        pose_stamp_on_replay_nv12_ex(work, HIT_REPLAY_W, HIT_REPLAY_H, HIT_REPLAY_W, pose);
    }
    hit_replay_nv12_to_rgb24(work, rgb, HIT_REPLAY_W, HIT_REPLAY_H, TD_FALSE);
    free(work);
    fp = fopen(path, "wb");
    if (fp == TD_NULL) {
        free(rgb);
        return TD_FAILURE;
    }
    (void)fprintf(fp, "P6\n%u %u\n255\n", HIT_REPLAY_W, HIT_REPLAY_H);
    if (fwrite(rgb, 1, rgb_sz, fp) != rgb_sz) {
        fclose(fp);
        free(rgb);
        return TD_FAILURE;
    }
    fclose(fp);
    free(rgb);
    return TD_SUCCESS;
}

static td_s32 hit_replay_write_raw_frame(const char *raw_dir, unsigned int idx, const unsigned char *nv12,
    const pose_result_t *pose, td_bool pose_valid)
{
    char nv12_path[320];
    char pose_path[320];
    FILE *fp = TD_NULL;
    td_u8 valid_flag = (pose_valid == TD_TRUE) ? 1U : 0U;

    (void)snprintf_s(nv12_path, sizeof(nv12_path), sizeof(nv12_path) - 1,
        "%s/frame_%04u.nv12", raw_dir, idx);
    fp = fopen(nv12_path, "wb");
    if (fp == TD_NULL) {
        return TD_FAILURE;
    }
    if (fwrite(nv12, 1, HIT_REPLAY_FRAME_BYTES, fp) != HIT_REPLAY_FRAME_BYTES) {
        fclose(fp);
        return TD_FAILURE;
    }
    fclose(fp);

    (void)snprintf_s(pose_path, sizeof(pose_path), sizeof(pose_path) - 1,
        "%s/frame_%04u.pose", raw_dir, idx);
    fp = fopen(pose_path, "wb");
    if (fp == TD_NULL) {
        return TD_FAILURE;
    }
    if (fwrite(&valid_flag, 1, 1, fp) != 1) {
        fclose(fp);
        return TD_FAILURE;
    }
    if ((valid_flag != 0U) && (pose != TD_NULL) &&
        fwrite(pose, 1, sizeof(pose_result_t), fp) != sizeof(pose_result_t)) {
        fclose(fp);
        return TD_FAILURE;
    }
    fclose(fp);
    return TD_SUCCESS;
}

static td_s32 hit_replay_read_raw_frame(const char *raw_dir, unsigned int idx, unsigned char *nv12,
    pose_result_t *pose, td_bool *pose_valid)
{
    char nv12_path[320];
    char pose_path[320];
    FILE *fp = TD_NULL;
    td_u8 valid_flag = 0;

    (void)snprintf_s(nv12_path, sizeof(nv12_path), sizeof(nv12_path) - 1,
        "%s/frame_%04u.nv12", raw_dir, idx);
    fp = fopen(nv12_path, "rb");
    if (fp == TD_NULL) {
        return TD_FAILURE;
    }
    if (fread(nv12, 1, HIT_REPLAY_FRAME_BYTES, fp) != HIT_REPLAY_FRAME_BYTES) {
        fclose(fp);
        return TD_FAILURE;
    }
    fclose(fp);

    if (pose_valid != TD_NULL) {
        *pose_valid = TD_FALSE;
    }
    (void)snprintf_s(pose_path, sizeof(pose_path), sizeof(pose_path) - 1,
        "%s/frame_%04u.pose", raw_dir, idx);
    fp = fopen(pose_path, "rb");
    if (fp == TD_NULL) {
        return TD_SUCCESS;
    }
    if (fread(&valid_flag, 1, 1, fp) != 1) {
        fclose(fp);
        return TD_FAILURE;
    }
    if ((valid_flag != 0U) && (pose != TD_NULL) &&
        fread(pose, 1, sizeof(pose_result_t), fp) == sizeof(pose_result_t)) {
        if (pose_valid != TD_NULL) {
            *pose_valid = TD_TRUE;
        }
    }
    fclose(fp);
    return TD_SUCCESS;
}

typedef struct {
    char session[96];
    int hit_idx;
    unsigned int frame_count;
    unsigned char *frames[HIT_REPLAY_TOTAL_FRAMES];
    pose_result_t poses[HIT_REPLAY_TOTAL_FRAMES];
    td_bool pose_valid[HIT_REPLAY_TOTAL_FRAMES];
} hit_replay_export_job_t;

typedef struct {
    char session[96];
    int hit_idx;
} hit_replay_pose_job_t;

static td_void *hit_replay_pose_render_thread(td_void *arg)
{
    hit_replay_pose_job_t *job = (hit_replay_pose_job_t *)arg;
    char out_dir[256];
    char raw_dir[280];
    char meta_path[280];
    char done_path[280];
    char mp4_path[280];
    char cmd[640];
    unsigned int count = 0;
    unsigned int i;
    td_s32 fps;

    if (job == TD_NULL) {
        return TD_NULL;
    }
    fps = hit_replay_env_int("WIDGET_REPLAY_FPS", HIT_REPLAY_FPS, 8, 30);
    (void)snprintf_s(out_dir, sizeof(out_dir), sizeof(out_dir) - 1, "%s/%s/hit_%d",
        hit_replay_dir_base(), job->session, job->hit_idx);
    (void)snprintf_s(raw_dir, sizeof(raw_dir), sizeof(raw_dir) - 1, "%s/raw", out_dir);
    (void)snprintf_s(meta_path, sizeof(meta_path), sizeof(meta_path) - 1, "%s/meta.txt", out_dir);
    {
        FILE *mr = fopen(meta_path, "r");
        char line[96];
        if (mr != TD_NULL) {
            while (fgets(line, (int)sizeof(line), mr) != TD_NULL) {
                unsigned int c = 0;
                if (sscanf(line, "count=%u", &c) == 1) {
                    count = c;
                }
            }
            fclose(mr);
        }
    }
    if (count == 0) {
        printf("hit_replay: pose render skip session=%s hit=%d (no meta count)\n", job->session, job->hit_idx);
        free(job);
        return TD_NULL;
    }
    for (i = 1; i <= count; i++) {
        char frame_path[320];
        unsigned char *nv12 = TD_NULL;
        pose_result_t pose;
        td_bool pose_valid = TD_FALSE;

        nv12 = (unsigned char *)malloc(HIT_REPLAY_FRAME_BYTES);
        if (nv12 == TD_NULL) {
            break;
        }
        if (hit_replay_read_raw_frame(raw_dir, i, nv12, &pose, &pose_valid) != TD_SUCCESS) {
            free(nv12);
            break;
        }
        (void)snprintf_s(frame_path, sizeof(frame_path), sizeof(frame_path) - 1,
            "%s/frame_%04u.ppm", out_dir, i);
        (td_void)hit_replay_write_ppm(frame_path, nv12,
            pose_valid == TD_TRUE ? &pose : TD_NULL, pose_valid);
        free(nv12);
    }
    {
        FILE *mw = fopen(meta_path, "w");
        if (mw != TD_NULL) {
            (void)fprintf(mw, "width=%u\nheight=%u\nfps=%d\ncount=%u\npose=1\nstage=done\n",
                (unsigned int)HIT_REPLAY_W, (unsigned int)HIT_REPLAY_H, fps, count);
            fclose(mw);
        }
    }
    (void)snprintf_s(mp4_path, sizeof(mp4_path), sizeof(mp4_path) - 1, "%s/%s/hit_%d.mp4",
        hit_replay_dir_base(), job->session, job->hit_idx);
    (void)snprintf_s(cmd, sizeof(cmd), sizeof(cmd) - 1,
        "command -v ffmpeg >/dev/null 2>&1 && ffmpeg -y -loglevel error -framerate %d "
        "-i '%s/frame_%%04d.ppm' -c:v libx264 -pix_fmt yuv420p -crf 20 -preset fast -an '%s' 2>/dev/null",
        fps, out_dir, mp4_path);
    (td_void)system(cmd);
    (void)snprintf_s(done_path, sizeof(done_path), sizeof(done_path) - 1, "%s/done.flag", out_dir);
    {
        FILE *done = fopen(done_path, "w");
        if (done != TD_NULL) {
            (void)fprintf(done, "ok\n");
            fclose(done);
        }
    }
    printf("hit_replay: pose rendered session=%s hit=%d frames=%u\n", job->session, job->hit_idx, count);
    free(job);
    return TD_NULL;
}

static td_void *hit_replay_export_thread(td_void *arg)
{
    hit_replay_export_job_t *job = (hit_replay_export_job_t *)arg;
    char out_dir[256];
    char raw_dir[280];
    char meta_path[280];
    char done_path[280];
    char mp4_path[280];
    char cmd[640];
    unsigned int i;
    td_s32 fps = hit_replay_env_int("WIDGET_REPLAY_FPS", HIT_REPLAY_FPS, 8, 30);
    td_bool pose_eager = (job != TD_NULL && job->hit_idx > 0 &&
        job->hit_idx <= g_hit_replay_pose_eager_max) ? TD_TRUE : TD_FALSE;

    if (job == TD_NULL) {
        return TD_NULL;
    }
    (void)snprintf_s(out_dir, sizeof(out_dir), sizeof(out_dir) - 1, "%s/%s/hit_%d",
        hit_replay_dir_base(), job->session, job->hit_idx);
    (void)hit_replay_mkdir_p(out_dir);
    if (pose_eager == TD_TRUE) {
        for (i = 0; i < job->frame_count; i++) {
            char frame_path[320];
            (void)snprintf_s(frame_path, sizeof(frame_path), sizeof(frame_path) - 1,
                "%s/frame_%04u.ppm", out_dir, i + 1);
            if (job->frames[i] != TD_NULL) {
                (td_void)hit_replay_write_ppm(frame_path, job->frames[i],
                    job->pose_valid[i] == TD_TRUE ? &job->poses[i] : TD_NULL, job->pose_valid[i]);
            }
        }
        (void)snprintf_s(mp4_path, sizeof(mp4_path), sizeof(mp4_path) - 1, "%s/%s/hit_%d.mp4",
            hit_replay_dir_base(), job->session, job->hit_idx);
        (void)snprintf_s(cmd, sizeof(cmd), sizeof(cmd) - 1,
            "command -v ffmpeg >/dev/null 2>&1 && ffmpeg -y -loglevel error -framerate %d "
            "-i '%s/frame_%%04d.ppm' -c:v libx264 -pix_fmt yuv420p -crf 20 -preset fast -an '%s' 2>/dev/null",
            fps, out_dir, mp4_path);
        (td_void)system(cmd);
    } else {
        (void)snprintf_s(raw_dir, sizeof(raw_dir), sizeof(raw_dir) - 1, "%s/raw", out_dir);
        (void)hit_replay_mkdir_p(raw_dir);
        for (i = 0; i < job->frame_count; i++) {
            if (job->frames[i] != TD_NULL) {
                (td_void)hit_replay_write_raw_frame(raw_dir, i + 1, job->frames[i],
                    job->pose_valid[i] == TD_TRUE ? &job->poses[i] : TD_NULL, job->pose_valid[i]);
            }
        }
        printf("hit_replay: raw saved session=%s hit=%d frames=%u (pose deferred)\n",
            job->session, job->hit_idx, job->frame_count);
    }
    (void)snprintf_s(meta_path, sizeof(meta_path), sizeof(meta_path) - 1, "%s/meta.txt", out_dir);
    {
        FILE *meta = fopen(meta_path, "w");
        if (meta != TD_NULL) {
            if (pose_eager == TD_TRUE) {
                (void)fprintf(meta, "width=%u\nheight=%u\nfps=%d\ncount=%u\npose=1\nstage=done\n",
                    (unsigned int)HIT_REPLAY_W, (unsigned int)HIT_REPLAY_H, fps, job->frame_count);
            } else {
                (void)fprintf(meta, "width=%u\nheight=%u\nfps=%d\ncount=%u\npose=0\nstage=raw\n",
                    (unsigned int)HIT_REPLAY_W, (unsigned int)HIT_REPLAY_H, fps, job->frame_count);
            }
            fclose(meta);
        }
    }
    (void)snprintf_s(done_path, sizeof(done_path), sizeof(done_path) - 1, "%s/done.flag", out_dir);
    {
        FILE *done = fopen(done_path, "w");
        if (done != TD_NULL) {
            (void)fprintf(done, "ok\n");
            fclose(done);
        }
    }
    if (pose_eager == TD_TRUE) {
        printf("hit_replay: exported session=%s hit=%d frames=%u dir=%s\n",
            job->session, job->hit_idx, job->frame_count, out_dir);
    }
    for (i = 0; i < job->frame_count; i++) {
        free(job->frames[i]);
        job->frames[i] = TD_NULL;
    }
    free(job);
    return TD_NULL;
}

static td_void hit_replay_start_export(td_void)
{
    hit_replay_export_job_t *job = TD_NULL;
    unsigned int i;
    pthread_t tid;

    if (g_hit_replay_export_count == 0) {
        return;
    }
    job = (hit_replay_export_job_t *)calloc(1, sizeof(*job));
    if (job == TD_NULL) {
        return;
    }
    (void)strncpy_s(job->session, sizeof(job->session), g_hit_replay_session, sizeof(job->session) - 1);
    job->hit_idx = g_hit_replay_hit_idx;
    job->frame_count = 0;
    for (i = 0; i < g_hit_replay_export_count; i++) {
        if (g_hit_replay_export[i].nv12 == TD_NULL) {
            continue;
        }
        job->frames[job->frame_count] = (unsigned char *)malloc(HIT_REPLAY_FRAME_BYTES);
        if (job->frames[job->frame_count] == TD_NULL) {
            break;
        }
        (void)memcpy_s(job->frames[job->frame_count], HIT_REPLAY_FRAME_BYTES,
            g_hit_replay_export[i].nv12, HIT_REPLAY_FRAME_BYTES);
        job->poses[job->frame_count] = g_hit_replay_export[i].pose;
        job->pose_valid[job->frame_count] = g_hit_replay_export[i].pose_valid;
        job->frame_count++;
    }
    g_hit_replay_export_count = 0;
    if (pthread_create(&tid, TD_NULL, hit_replay_export_thread, job) != 0) {
        for (i = 0; i < job->frame_count; i++) {
            free(job->frames[i]);
        }
        free(job);
        printf("hit_replay: export thread create failed\n");
        return;
    }
    (void)pthread_detach(tid);
}

static td_bool hit_replay_pending_push_locked(const char *session, int hit_idx)
{
    unsigned int next_wr = (g_hit_replay_pending_wr + 1) % HIT_REPLAY_PENDING_MAX;

    if (next_wr == g_hit_replay_pending_rd) {
        printf("hit_replay: pending queue full, drop hit=%d\n", hit_idx);
        return TD_FALSE;
    }
    (void)strncpy_s(g_hit_replay_pending[g_hit_replay_pending_wr].session,
        sizeof(g_hit_replay_pending[g_hit_replay_pending_wr].session), session,
        sizeof(g_hit_replay_pending[g_hit_replay_pending_wr].session) - 1);
    g_hit_replay_pending[g_hit_replay_pending_wr].hit_idx = hit_idx;
    g_hit_replay_pending_wr = next_wr;
    return TD_TRUE;
}

static td_bool hit_replay_pending_pop_locked(char *session, size_t session_sz, int *hit_idx)
{
    if (g_hit_replay_pending_rd == g_hit_replay_pending_wr) {
        return TD_FALSE;
    }
    (void)strncpy_s(session, session_sz, g_hit_replay_pending[g_hit_replay_pending_rd].session, session_sz - 1);
    *hit_idx = g_hit_replay_pending[g_hit_replay_pending_rd].hit_idx;
    g_hit_replay_pending_rd = (g_hit_replay_pending_rd + 1) % HIT_REPLAY_PENDING_MAX;
    return TD_TRUE;
}

static td_void hit_replay_start_capture_locked(const char *session, int hit_idx)
{
    unsigned int i;
    unsigned int start;
    unsigned int count;
    unsigned int dst;

    (void)strncpy_s(g_hit_replay_session, sizeof(g_hit_replay_session), session, sizeof(g_hit_replay_session) - 1);
    g_hit_replay_hit_idx = hit_idx;
    g_hit_replay_export_count = 0;
    count = 0;
    if (g_replay_live_ring != 0) {
        count = g_hit_replay_ring_count;
        start = (count >= HIT_REPLAY_PRE_FRAMES)
            ? g_hit_replay_ring_head
            : ((HIT_REPLAY_PRE_FRAMES + g_hit_replay_ring_head - count) % HIT_REPLAY_PRE_FRAMES);
        for (i = 0; (i < count) && (i < HIT_REPLAY_PRE_FRAMES); i++) {
            unsigned int src_idx = (start + i) % HIT_REPLAY_PRE_FRAMES;

            if (g_hit_replay_export_count >= HIT_REPLAY_TOTAL_FRAMES) {
                break;
            }
            if (g_hit_replay_ring[src_idx].nv12 == TD_NULL) {
                continue;
            }
            dst = g_hit_replay_export_count++;
            if (hit_replay_export_slot(dst) != TD_NULL) {
                hit_replay_copy_slot(&g_hit_replay_export[dst], &g_hit_replay_ring[src_idx]);
            }
        }
    }
    g_hit_replay_capturing_post = TD_TRUE;
    g_hit_replay_post_left = HIT_REPLAY_POST_FRAMES;
    printf("hit_replay: trigger session=%s hit=%d pre=%u post=%u live=%d pose_eager=%d\n",
        session, hit_idx, count, (unsigned int)HIT_REPLAY_POST_FRAMES, g_replay_live_ring,
        (hit_idx <= g_hit_replay_pose_eager_max) ? 1 : 0);
}

static td_void hit_replay_drain_pending_locked(td_void)
{
    char session[96];
    int hit_idx = 0;

    while ((g_hit_replay_capturing_post == TD_FALSE) &&
        (hit_replay_pending_pop_locked(session, sizeof(session), &hit_idx) == TD_TRUE)) {
        hit_replay_start_capture_locked(session, hit_idx);
        break;
    }
}

static td_void hit_replay_request_capture(const char *session, int hit_idx)
{
    if ((session == TD_NULL) || (session[0] == '\0') || (hit_idx <= 0)) {
        return;
    }
    if (g_hit_replay_inited != TD_TRUE) {
        hit_replay_init_once();
    }
    if (g_hit_replay_inited != TD_TRUE) {
        return;
    }

    (void)pthread_mutex_lock(&g_hit_replay_mutex);
    if (g_hit_replay_capturing_post == TD_TRUE) {
        if (hit_replay_pending_push_locked(session, hit_idx) == TD_TRUE) {
            printf("hit_replay: queued session=%s hit=%d (capturing)\n", session, hit_idx);
        }
    } else {
        hit_replay_start_capture_locked(session, hit_idx);
    }
    (void)pthread_mutex_unlock(&g_hit_replay_mutex);
}

static td_void hit_replay_scale_to_out(const unsigned char *resize_src, td_u32 src_w, td_u32 src_h,
    td_u32 src_stride_y, td_u32 src_stride_uv, td_bool src_is_nv21, unsigned char *dst)
{
    if ((src_w * HIT_REPLAY_H) == (src_h * HIT_REPLAY_W)) {
        vio_ai_resize_yuv420sp_bilinear(resize_src, src_w, src_h, src_stride_y, src_stride_uv,
            dst, HIT_REPLAY_W, HIT_REPLAY_H, src_is_nv21, TD_FALSE);
    } else {
        (td_void)vio_ai_resize_yuv420sp_letterbox(resize_src, src_w, src_h, src_stride_y, src_stride_uv,
            dst, HIT_REPLAY_W, HIT_REPLAY_H, src_is_nv21, TD_FALSE);
    }
}

static td_void hit_replay_push_scaled(const ot_video_frame_info *frame_info)
{
    const ot_video_frame *vf = &frame_info->video_frame;
    td_u32 src_w = vf->width;
    td_u32 src_h = vf->height;
    td_u32 src_stride_y = vf->stride[0];
    td_u32 src_stride_uv = vf->stride[1];
    td_bool src_is_nv21 = hit_replay_src_is_nv21(vf->pixel_format);
    const unsigned char *src_y = TD_NULL;
    const unsigned char *src_uv = TD_NULL;
    void *y_mapped = TD_NULL;
    void *uv_mapped = TD_NULL;
    unsigned char *slot = TD_NULL;
    unsigned int ring_idx;

    if ((g_hit_replay_inited != TD_TRUE) || (src_w == 0) || (src_h == 0)) {
        return;
    }

    src_y = (const unsigned char *)vf->virt_addr[0];
    if (src_y == TD_NULL) {
        size_t y_sz = (size_t)src_stride_y * src_h;
        size_t uv_sz = (size_t)src_stride_uv * (src_h / 2);
        y_mapped = ss_mpi_sys_mmap(vf->phys_addr[0], y_sz);
        uv_mapped = ss_mpi_sys_mmap(vf->phys_addr[1], uv_sz);
        if ((y_mapped == TD_NULL) || (uv_mapped == TD_NULL)) {
            if (y_mapped != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(y_mapped, y_sz);
            }
            if (uv_mapped != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(uv_mapped, uv_sz);
            }
            return;
        }
        src_y = (const unsigned char *)y_mapped;
        src_uv = (const unsigned char *)uv_mapped;
    } else {
        src_uv = (const unsigned char *)vf->virt_addr[1];
        if (src_uv == TD_NULL) {
            src_uv = src_y + (size_t)src_stride_y * src_h;
        }
    }

    (void)pthread_mutex_lock(&g_hit_replay_mutex);
    if (g_replay_live_ring != 0) {
        ring_idx = g_hit_replay_ring_head;
        g_hit_replay_ring_head = (g_hit_replay_ring_head + 1) % HIT_REPLAY_PRE_FRAMES;
        if (g_hit_replay_ring_count < HIT_REPLAY_PRE_FRAMES) {
            g_hit_replay_ring_count++;
        }
        slot = hit_replay_ring_slot(ring_idx);
        if ((slot != TD_NULL) && (src_y != TD_NULL) && (src_uv != TD_NULL)) {
            const unsigned char *resize_src = src_y;
            unsigned char *packed_src = TD_NULL;
            if (src_uv != src_y + (size_t)src_stride_y * src_h) {
                size_t y_sz = (size_t)src_stride_y * src_h;
                size_t uv_sz = (size_t)src_stride_uv * (src_h / 2);
                packed_src = (unsigned char *)malloc(y_sz + uv_sz);
                if (packed_src != TD_NULL) {
                    (void)memcpy_s(packed_src, y_sz + uv_sz, src_y, y_sz);
                    (void)memcpy_s(packed_src + y_sz, uv_sz, src_uv, uv_sz);
                    resize_src = packed_src;
                }
            }
            hit_replay_scale_to_out(resize_src, src_w, src_h, src_stride_y, src_stride_uv, src_is_nv21, slot);
            hit_replay_snapshot_pose(&g_hit_replay_ring[ring_idx]);
            free(packed_src);
        }

        if ((g_hit_replay_capturing_post == TD_TRUE) &&
            (g_hit_replay_export_count < HIT_REPLAY_TOTAL_FRAMES) && (slot != TD_NULL)) {
            unsigned int exp_idx = g_hit_replay_export_count;

            if (hit_replay_export_slot(exp_idx) != TD_NULL) {
                hit_replay_copy_slot(&g_hit_replay_export[exp_idx], &g_hit_replay_ring[ring_idx]);
                g_hit_replay_export_count++;
            }
        }
    } else if ((g_hit_replay_capturing_post == TD_TRUE) &&
        (g_hit_replay_export_count < HIT_REPLAY_TOTAL_FRAMES) &&
        (src_y != TD_NULL) && (src_uv != TD_NULL)) {
        unsigned int exp_idx = g_hit_replay_export_count;
        unsigned char *exp = hit_replay_export_slot(exp_idx);
        if (exp != TD_NULL) {
            const unsigned char *resize_src = src_y;
            unsigned char *packed_src = TD_NULL;
            if (src_uv != src_y + (size_t)src_stride_y * src_h) {
                size_t y_sz = (size_t)src_stride_y * src_h;
                size_t uv_sz = (size_t)src_stride_uv * (src_h / 2);
                packed_src = (unsigned char *)malloc(y_sz + uv_sz);
                if (packed_src != TD_NULL) {
                    (void)memcpy_s(packed_src, y_sz + uv_sz, src_y, y_sz);
                    (void)memcpy_s(packed_src + y_sz, uv_sz, src_uv, uv_sz);
                    resize_src = packed_src;
                }
            }
            hit_replay_scale_to_out(resize_src, src_w, src_h, src_stride_y, src_stride_uv, src_is_nv21, exp);
            hit_replay_snapshot_pose(&g_hit_replay_export[exp_idx]);
            free(packed_src);
            g_hit_replay_export_count++;
        }
    }

    if ((g_hit_replay_capturing_post == TD_TRUE) && (g_hit_replay_post_left > 0)) {
        g_hit_replay_post_left--;
        if (g_hit_replay_post_left == 0) {
            g_hit_replay_capturing_post = TD_FALSE;
            hit_replay_start_export();
            hit_replay_drain_pending_locked();
        }
    }
    (void)pthread_mutex_unlock(&g_hit_replay_mutex);

    if (y_mapped != TD_NULL) {
        (td_void)ss_mpi_sys_munmap(y_mapped, (size_t)src_stride_y * src_h);
    }
    if (uv_mapped != TD_NULL) {
        (td_void)ss_mpi_sys_munmap(uv_mapped, (size_t)src_stride_uv * (src_h / 2));
    }
}

static td_void hit_replay_process_frame(const ot_video_frame_info *frame_info)
{
    struct timeval now;
    double dt;
    td_s32 fps = hit_replay_env_int("WIDGET_REPLAY_FPS", HIT_REPLAY_FPS, 8, 30);
    double min_dt = 1.0 / (double)fps;

    hit_replay_init_once();
    if (g_hit_replay_inited != TD_TRUE) {
        return;
    }
    if (gettimeofday(&now, TD_NULL) != 0) {
        return;
    }
    if (g_hit_replay_last_push.tv_sec != 0) {
        dt = (double)(now.tv_sec - g_hit_replay_last_push.tv_sec) +
            (double)(now.tv_usec - g_hit_replay_last_push.tv_usec) / 1000000.0;
        if (dt < min_dt) {
            return;
        }
    }
    g_hit_replay_last_push = now;
    hit_replay_push_scaled(frame_info);
}

static td_bool hit_replay_copy_submit_locked(const ot_video_frame_info *frame_info)
{
    const ot_video_frame *vf = &frame_info->video_frame;
    size_t y_sz;
    size_t uv_sz;

    if (frame_info == TD_NULL || vf->width == 0 || vf->height == 0) {
        return TD_FALSE;
    }
    y_sz = (size_t)vf->stride[0] * vf->height;
    uv_sz = (size_t)vf->stride[1] * (vf->height / 2U);
    if ((y_sz + uv_sz) > HIT_REPLAY_SUBMIT_BYTES) {
        return TD_FALSE;
    }
    if (g_replay_submit.nv12 == TD_NULL) {
        g_replay_submit.nv12 = (unsigned char *)malloc(HIT_REPLAY_SUBMIT_BYTES);
        if (g_replay_submit.nv12 == TD_NULL) {
            return TD_FALSE;
        }
    }
    if (vf->virt_addr[0] != TD_NULL) {
        (td_void)memcpy_s(g_replay_submit.nv12, HIT_REPLAY_SUBMIT_BYTES, vf->virt_addr[0], y_sz);
        if (vf->virt_addr[1] != TD_NULL) {
            (td_void)memcpy_s(g_replay_submit.nv12 + y_sz, HIT_REPLAY_SUBMIT_BYTES - y_sz,
                vf->virt_addr[1], uv_sz);
        } else {
            (td_void)memcpy_s(g_replay_submit.nv12 + y_sz, HIT_REPLAY_SUBMIT_BYTES - y_sz,
                vf->virt_addr[0] + y_sz, uv_sz);
        }
    } else {
        void *y_map = ss_mpi_sys_mmap(vf->phys_addr[0], y_sz);
        void *uv_map = ss_mpi_sys_mmap(vf->phys_addr[1], uv_sz);
        if (y_map == TD_NULL || uv_map == TD_NULL) {
            if (y_map != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(y_map, y_sz);
            }
            if (uv_map != TD_NULL) {
                (td_void)ss_mpi_sys_munmap(uv_map, uv_sz);
            }
            return TD_FALSE;
        }
        (td_void)memcpy_s(g_replay_submit.nv12, HIT_REPLAY_SUBMIT_BYTES, y_map, y_sz);
        (td_void)memcpy_s(g_replay_submit.nv12 + y_sz, HIT_REPLAY_SUBMIT_BYTES - y_sz, uv_map, uv_sz);
        (td_void)ss_mpi_sys_munmap(y_map, y_sz);
        (td_void)ss_mpi_sys_munmap(uv_map, uv_sz);
    }
    g_replay_submit.width = vf->width;
    g_replay_submit.height = vf->height;
    g_replay_submit.stride_y = vf->stride[0];
    g_replay_submit.stride_uv = vf->stride[1];
    g_replay_submit.is_nv21 = vio_ai_pixel_format_is_nv21(vf->pixel_format);
    g_replay_submit.pixel_format = vf->pixel_format;
    g_replay_submit.pending = TD_TRUE;
    return TD_TRUE;
}

td_bool hit_replay_uses_dedicated_chn(td_void)
{
    hit_replay_init_once();
    return (g_replay_src_chn >= 0) && ((ot_vpss_chn)g_replay_src_chn != g_attach_chn);
}

static td_void *hit_replay_worker_thread(td_void *arg)
{
    ot_video_frame_info frame_info;

    (void)arg;
    while (g_replay_worker_run != 0) {
        if (hit_replay_needs_frames() != TD_TRUE) {
            hit_replay_deinit();
            usleep(100000);
            continue;
        }

        if (hit_replay_uses_dedicated_chn() == TD_TRUE) {
            ot_video_frame_info replay_frame;
            td_s32 get_ret;

            (td_void)memset_s(&replay_frame, sizeof(replay_frame), 0, sizeof(replay_frame));
            get_ret = ss_mpi_vpss_get_chn_frame(g_attach_grp, (ot_vpss_chn)g_replay_src_chn,
                &replay_frame, 0);
            if (get_ret != TD_SUCCESS) {
                usleep(30000);
                continue;
            }
            hit_replay_process_frame(&replay_frame);
            (td_void)ss_mpi_vpss_release_chn_frame(g_attach_grp, (ot_vpss_chn)g_replay_src_chn,
                &replay_frame);
            continue;
        }

        (td_void)pthread_mutex_lock(&g_replay_worker_mtx);
        while ((g_replay_worker_run != 0) && (g_replay_submit.pending != TD_TRUE)) {
            (td_void)pthread_cond_wait(&g_replay_worker_cv, &g_replay_worker_mtx);
        }
        if (g_replay_worker_run == 0) {
            (td_void)pthread_mutex_unlock(&g_replay_worker_mtx);
            break;
        }
        (td_void)memset_s(&frame_info, sizeof(frame_info), 0, sizeof(frame_info));
        frame_info.video_frame.width = g_replay_submit.width;
        frame_info.video_frame.height = g_replay_submit.height;
        frame_info.video_frame.stride[0] = g_replay_submit.stride_y;
        frame_info.video_frame.stride[1] = g_replay_submit.stride_uv;
        frame_info.video_frame.pixel_format = g_replay_submit.pixel_format;
        frame_info.video_frame.virt_addr[0] = g_replay_submit.nv12;
        frame_info.video_frame.virt_addr[1] = g_replay_submit.nv12 +
            (size_t)g_replay_submit.stride_y * g_replay_submit.height;
        g_replay_submit.pending = TD_FALSE;
        (td_void)pthread_mutex_unlock(&g_replay_worker_mtx);

        if (hit_replay_needs_frames() != TD_TRUE) {
            hit_replay_deinit();
            continue;
        }
        hit_replay_process_frame(&frame_info);
    }
    return TD_NULL;
}

static td_void hit_replay_worker_start(td_void)
{
    if (g_replay_worker_on == TD_TRUE) {
        return;
    }
    g_replay_worker_run = 1;
    if (pthread_create(&g_replay_worker_tid, TD_NULL, hit_replay_worker_thread, TD_NULL) != 0) {
        g_replay_worker_run = 0;
        printf("hit_replay: worker thread create failed\n");
        return;
    }
    g_replay_worker_on = TD_TRUE;
    printf("hit_replay: async worker started\n");
}

td_void hit_replay_worker_stop(td_void)
{
    if (g_replay_worker_on != TD_TRUE) {
        return;
    }
    g_replay_worker_run = 0;
    (td_void)pthread_cond_broadcast(&g_replay_worker_cv);
    (td_void)pthread_join(g_replay_worker_tid, TD_NULL);
    g_replay_worker_on = TD_FALSE;
    if (g_replay_submit.nv12 != TD_NULL) {
        free(g_replay_submit.nv12);
        g_replay_submit.nv12 = TD_NULL;
    }
    g_replay_submit.pending = TD_FALSE;
}

void hit_replay_submit_frame(const ot_video_frame_info *frame_info)
{
    if (hit_replay_needs_frames() != TD_TRUE) {
        return;
    }
    hit_replay_init_once();

    /* VPSS ch3 等专用回放通道：worker 按 WIDGET_REPLAY_FPS 取帧，勿用 attach 方图 */
    if (hit_replay_uses_dedicated_chn() == TD_TRUE) {
        hit_replay_worker_start();
        return;
    }

    if (g_replay_live_ring != 0) {
        hit_replay_worker_start();
        if (g_replay_worker_on != TD_TRUE) {
            return;
        }
        (td_void)pthread_mutex_lock(&g_replay_worker_mtx);
        if (g_replay_submit.pending == TD_TRUE) {
            (td_void)pthread_mutex_unlock(&g_replay_worker_mtx);
            return;
        }
        if (hit_replay_copy_submit_locked(frame_info) == TD_TRUE) {
            (td_void)pthread_cond_signal(&g_replay_worker_cv);
        }
        (td_void)pthread_mutex_unlock(&g_replay_worker_mtx);
        return;
    }
    if (g_hit_replay_capturing_post == TD_TRUE) {
        hit_replay_process_frame(frame_info);
    }
}

static td_void hit_replay_feed_frame(const ot_video_frame_info *frame_info)
{
    hit_replay_submit_frame(frame_info);
}

static td_void hit_replay_request_pose_render(const char *session, int hit_idx)
{
    hit_replay_pose_job_t *job = TD_NULL;
    pthread_t tid;

    if ((session == TD_NULL) || (session[0] == '\0') || (hit_idx <= 0)) {
        return;
    }
    job = (hit_replay_pose_job_t *)calloc(1, sizeof(*job));
    if (job == TD_NULL) {
        return;
    }
    (void)strncpy_s(job->session, sizeof(job->session), session, sizeof(job->session) - 1);
    job->hit_idx = hit_idx;
    if (pthread_create(&tid, TD_NULL, hit_replay_pose_render_thread, job) != 0) {
        free(job);
        printf("hit_replay: pose render thread create failed session=%s hit=%d\n", session, hit_idx);
        return;
    }
    (void)pthread_detach(tid);
}

void hit_replay_poll_pose_trigger(td_void)
{
    FILE *fp = TD_NULL;
    char line[160];
    char session[96];
    int hit_idx = 0;
    td_bool got_any = TD_FALSE;

    fp = fopen(HIT_REPLAY_POSE_REQ_PATH, "r");
    if (fp == TD_NULL) {
        return;
    }
    while (fgets(line, (int)sizeof(line), fp) != TD_NULL) {
        if (sscanf(line, "%95s %d", session, &hit_idx) != 2) {
            continue;
        }
        hit_replay_request_pose_render(session, hit_idx);
        got_any = TD_TRUE;
    }
    fclose(fp);
    if (got_any == TD_TRUE) {
        (void)remove(HIT_REPLAY_POSE_REQ_PATH);
    }
}

void hit_replay_poll_trigger(td_void)
{
    FILE *fp = TD_NULL;
    char line[160];
    char session[96];
    int hit_idx = 0;
    td_bool got_any = TD_FALSE;

    fp = fopen(HIT_REPLAY_REQ_PATH, "r");
    if (fp == TD_NULL) {
        return;
    }
    while (fgets(line, (int)sizeof(line), fp) != TD_NULL) {
        if (sscanf(line, "%95s %d", session, &hit_idx) != 2) {
            continue;
        }
        hit_replay_request_capture(session, hit_idx);
        got_any = TD_TRUE;
    }
    fclose(fp);
    if (got_any == TD_TRUE) {
        (void)remove(HIT_REPLAY_REQ_PATH);
    }
}
