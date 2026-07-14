/*
 * vo_gfbg_init.c — SS928：camera_pipe 或 modelzoo YOLOv8 + GFBG + Qt 面板（version3.0）
 *
 * 默认：camera_pipe 预览 + bin/sample_vio_ai attach（VPSS ch1 推理，CORNER_RECTEX 画框）。
 * 可选 WIDGET_AI_BACKEND=modelzoo 使用 /opt/sample/yolov8（独占 VI，与面板摄像头互斥）。
 *
 * 交叉编译：version3.0/scripts/build_vo_gfbg_init.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "ot_type.h"
#include "ot_common.h"
#include "gfbg.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "camera_pipe.h"

#define PANEL_PATH "/opt/widget_ui/widget_panel"
#define DEFAULT_AI_MODEL "/opt/widget_ui/models/best_aipp_fix.om"
#define DEFAULT_AI_ATTACH_BIN "/opt/widget_ui/bin/sample_vio_ai"
#define DEFAULT_YOLOV8_BIN "/opt/sample/yolov8/sample_yolov8_os08a20"
#define DEFAULT_YOLOV8_WORKDIR "/opt/sample/yolov8"
#define DEFAULT_YOLOV8_OM_NAME "yolov8n.om"
#define AI_ATTACH_LOG_PATH "/tmp/sample_vio_ai.log"
#define AI_MODELZOO_LOG_PATH "/tmp/sample_yolov8.log"
/* Qt 写入一行：show x y w h；show=0 隐藏 VO */
#define WIDGET_CAM_VO_STATE "/tmp/.widget_cam_vo"

#define LOG(fmt, ...) fprintf(stderr, "[vo_gfbg] " fmt "\n", ##__VA_ARGS__)
#define PANEL_LOG_PATH "/tmp/widget_panel.log"

static volatile int g_camvo_thread_run;
static pthread_t g_camvo_thread;
static volatile sig_atomic_t g_stop_requested = 0;

static td_void vo_gfbg_reset_camvo_file(td_void)
{
    FILE *fp = fopen(WIDGET_CAM_VO_STATE, "we");
    if (fp != NULL) {
        (void)fprintf(fp, "0 0 0 0 0\n");
        (void)fclose(fp);
    }
}

static td_void vo_gfbg_on_stop_signal(int sig)
{
    (void)sig;
    g_stop_requested = 1;
}

static td_bool widget_cam_vo_visible(td_void)
{
    FILE *fp = fopen(WIDGET_CAM_VO_STATE, "re");
    int s = 0, x = 0, y = 0, w = 0, h = 0;
    td_bool visible = TD_FALSE;

    if (fp == NULL) {
        return TD_FALSE;
    }
    if (fscanf(fp, " %d %d %d %d %d ", &s, &x, &y, &w, &h) >= 1) {
        visible = (s != 0 && w >= 32 && h >= 32) ? TD_TRUE : TD_FALSE;
    }
    (void)fclose(fp);
    return visible;
}

static td_void widget_ai_try_start_on_cam_show(td_void);

static void *camvo_state_thread(void *arg)
{
    int s_last = -2;
    int x_last = 0, y_last = 0, w_last = 0, h_last = 0;
    int hide_pending = 0;
    int x_hold = 0, y_hold = 0, w_hold = 0, h_hold = 0;

    (void)arg;
    while (g_camvo_thread_run) {
        FILE *fp = fopen(WIDGET_CAM_VO_STATE, "re");
        if (fp != NULL) {
            int s = 0, x = 0, y = 0, w = 0, h = 0;
            if (fscanf(fp, " %d %d %d %d %d ", &s, &x, &y, &w, &h) >= 1) {
                if (s != 0) {
                    hide_pending = 0;
                    x_hold = x;
                    y_hold = y;
                    w_hold = w;
                    h_hold = h;
                } else {
                    hide_pending++;
                }

                if (s != 0) {
                    if (s != s_last || x != x_last || y != y_last || w != w_last || h != h_last) {
                        s_last = s;
                        x_last = x;
                        y_last = y;
                        w_last = w;
                        h_last = h;
                        (void)camera_pipe_vo_set_window((td_u32)x, (td_u32)y, (td_u32)w, (td_u32)h, TD_TRUE);
                    }
                } else if (hide_pending >= 8 || s_last == -2) {
                    /* 首次或连续 hide：立即关 VO，避免开机全屏闪摄像头 */
                    if (s_last != 0) {
                        s_last = 0;
                        x_last = 0;
                        y_last = 0;
                        w_last = 0;
                        h_last = 0;
                        (void)camera_pipe_vo_set_window(0U, 0U, 0U, 0U, TD_FALSE);
                    }
                } else if (s_last != 0 && w_hold >= 32 && h_hold >= 32) {
                    /* 短暂 hide 信号：保持上一帧有效窗口 */
                    (void)camera_pipe_vo_set_window((td_u32)x_hold, (td_u32)y_hold,
                        (td_u32)w_hold, (td_u32)h_hold, TD_TRUE);
                }
            }
            (void)fclose(fp);
        }
        widget_ai_try_start_on_cam_show();
        usleep(40000);
    }
    return NULL;
}

static td_void camvo_thread_start(td_void)
{
    g_camvo_thread_run = 1;
    if (pthread_create(&g_camvo_thread, NULL, camvo_state_thread, NULL) != 0) {
        LOG("pthread_create(camvo) failed: %m");
        g_camvo_thread_run = 0;
    } else {
        LOG("camvo state thread -> %s", WIDGET_CAM_VO_STATE);
    }
}

static td_void camvo_thread_stop(td_void)
{
    if (g_camvo_thread_run == 0) {
        return;
    }
    g_camvo_thread_run = 0;
    (void)pthread_join(g_camvo_thread, NULL);
    LOG("camvo state thread joined");
}

static td_s32 gfbg_init(td_void)
{
    ot_fb_layer_info layer_info;
    ot_fb_alpha alpha;
    ot_fb_colorkey colorkey;
    td_bool enable;
    td_s32 fb_fd;
    td_s32 ret;
    struct fb_var_screeninfo var;
    struct fb_bitfield g_a16 = {15, 1, 0};
    struct fb_bitfield g_r16 = {10, 5, 0};
    struct fb_bitfield g_g16 = {5, 5, 0};
    struct fb_bitfield g_b16 = {0, 5, 0};

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        LOG("open /dev/fb0 failed: %m");
        return TD_FAILURE;
    }

    enable = TD_FALSE;
    ioctl(fb_fd, FBIOPUT_SHOW_GFBG, &enable);

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        LOG("FBIOGET_VSCREENINFO failed: %m");
        close(fb_fd);
        return TD_FAILURE;
    }
    LOG("vinfo before: %dx%d, bpp=%d, virtual=%dx%d",
        var.xres, var.yres, var.bits_per_pixel,
        var.xres_virtual, var.yres_virtual);

    var.xres = 1920;
    var.yres = 1080;
    var.xres_virtual = 1920;
    var.yres_virtual = 1080;
    var.bits_per_pixel = 16;
    var.transp = g_a16;
    var.red = g_r16;
    var.green = g_g16;
    var.blue = g_b16;
    var.activate = FB_ACTIVATE_NOW;

    ret = ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var);
    if (ret < 0) {
        LOG("FBIOPUT_VSCREENINFO 失败: %m（使用驱动预设值）");
    } else {
        LOG("vinfo 已设为 1920x1080 ARGB1555, virtual=1920x1080");
    }

    memset(&layer_info, 0, sizeof(layer_info));
    layer_info.buf_mode = OT_FB_LAYER_BUF_NONE;
    layer_info.mask = OT_FB_LAYER_MASK_BUF_MODE;
    if (ioctl(fb_fd, FBIOPUT_LAYER_INFO, &layer_info) < 0) {
        LOG("FBIOPUT_LAYER_INFO (BUF_NONE) failed: %m");
        close(fb_fd);
        return TD_FAILURE;
    }
    LOG("BUF_NONE 设置成功");

    enable = TD_FALSE;
    ioctl(fb_fd, FBIOPUT_COMPRESSION_GFBG, &enable);

    memset(&alpha, 0, sizeof(alpha));
    alpha.alpha_en = TD_TRUE;
    alpha.alpha_chn_en = TD_FALSE;
    alpha.alpha0 = 0xFF;
    alpha.global_alpha = 0xFF;
    ioctl(fb_fd, FBIOPUT_ALPHA_GFBG, &alpha);

    memset(&colorkey, 0, sizeof(colorkey));
    colorkey.enable = TD_TRUE;
    colorkey.value = 0x000000;
    ioctl(fb_fd, FBIOPUT_COLORKEY_GFBG, &colorkey);

    enable = TD_TRUE;
    ioctl(fb_fd, FBIOPUT_SHOW_GFBG, &enable);

    LOG("GFBG init OK: vinfo=1920x1080/16bpp, BUF_NONE, yres_virtual=1080, alpha=255, colorkey=0");
    return TD_SUCCESS;
}

static pid_t g_ai_pid = -1;
static volatile int g_ai_watch_run = 0;
static pthread_t g_ai_watch_tid;
static volatile int g_ai_restart_delay_sec = 3;
static volatile int g_ai_fail_streak = 0;
static volatile time_t g_ai_pid_start = 0;
static volatile time_t g_ai_last_fork = 0;

static td_void widget_ai_reap_stale(td_void)
{
    if (g_ai_pid > 0) {
        (void)kill(g_ai_pid, SIGTERM);
        (void)waitpid(g_ai_pid, NULL, WNOHANG);
        g_ai_pid = -1;
    }
    (void)system("killall -q sample_vio_ai 2>/dev/null");
    usleep(300000);
}

static td_bool widget_ai_use_attach(td_void);
static td_s32 widget_ai_fork_attach(td_bool append_log);

static td_void widget_ai_try_start_on_cam_show(td_void)
{
    static int show_stable = 0;

    if (!widget_ai_use_attach()) {
        show_stable = 0;
        return;
    }
    if (g_ai_pid > 0) {
        show_stable = 0;
        return;
    }
    if (widget_cam_vo_visible() != TD_TRUE) {
        show_stable = 0;
        return;
    }

    show_stable++;
    if (show_stable < 10) {
        return;
    }
    show_stable = 0;

    if ((time(NULL) - g_ai_last_fork) < 3) {
        return;
    }
    (void)widget_ai_fork_attach(TD_FALSE);
}

static td_s32 widget_ai_fork_attach(td_bool append_log)
{
    const char *model = getenv("WIDGET_AI_MODEL");
    const char *ai_bin = getenv("WIDGET_AI_BIN");
    const char *grp_env = getenv("WIDGET_AI_ATTACH_GRP");
    const char *chn_env = getenv("WIDGET_AI_ATTACH_CHN");
    char grp_buf[16];
    char chn_buf[16];
    const char *ld;
    char ld_buf[512];
    const char *log_mode = append_log ? "a" : "w";
    pid_t pid;

    if (!widget_ai_use_attach()) {
        return TD_SUCCESS;
    }
    if (camera_pipe_preview_active() != TD_TRUE) {
        LOG("attach deferred: camera preview not active");
        return TD_FAILURE;
    }
    widget_ai_reap_stale();

    if (model == NULL || model[0] == '\0') {
        model = DEFAULT_AI_MODEL;
    }
    if (ai_bin == NULL || ai_bin[0] == '\0') {
        ai_bin = DEFAULT_AI_ATTACH_BIN;
    }
    if (access(model, R_OK) != 0) {
        LOG("attach model not found: %s (skip)", model);
        return TD_SUCCESS;
    }
    if (access(ai_bin, X_OK) != 0) {
        LOG("attach binary not found: %s (skip)", ai_bin);
        return TD_SUCCESS;
    }

    (void)snprintf(grp_buf, sizeof(grp_buf), "%u",
        grp_env ? (td_u32)strtoul(grp_env, NULL, 0) : camera_pipe_vpss_grp());
    (void)snprintf(chn_buf, sizeof(chn_buf), "%u",
        chn_env ? (td_u32)strtoul(chn_env, NULL, 0) : camera_pipe_ai_vpss_chn());

    pid = fork();
    if (pid < 0) {
        LOG("fork attach AI failed: %m");
        return TD_FAILURE;
    }
    if (pid == 0) {
        ld = getenv("LD_LIBRARY_PATH");
        if (ld != NULL && ld[0] != '\0') {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:%s", ld);
        } else {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:/usr/lib/aarch64-linux-gnu");
        }
        setenv("LD_LIBRARY_PATH", ld_buf, 1);
        setenv("ASCEND_AICPU_KERNEL_PATH", "/opt/lib/npu", 1);
        if (freopen(AI_ATTACH_LOG_PATH, log_mode, stdout) == NULL) {
            /* continue */
        }
        if (freopen(AI_ATTACH_LOG_PATH, "a", stderr) == NULL) {
            /* continue */
        }
        execl(ai_bin, ai_bin, model, "attach", grp_buf, chn_buf, (char *)NULL);
        LOG("execl attach AI failed: %m");
        _exit(127);
    }

    g_ai_pid = pid;
    g_ai_pid_start = time(NULL);
    g_ai_last_fork = g_ai_pid_start;
    LOG("YOLO attach started pid=%d append=%d: %s %s attach %s %s",
        (int)pid, (int)append_log, ai_bin, model, grp_buf, chn_buf);
    usleep(350000);
    return TD_SUCCESS;
}

static void *widget_ai_watch_thread(void *arg)
{
    (void)arg;
    while (g_ai_watch_run != 0 && g_stop_requested == 0) {
        if (g_ai_pid > 0) {
            int st = 0;
            pid_t w = waitpid(g_ai_pid, &st, WNOHANG);
            if (w == g_ai_pid) {
                int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                time_t ran_sec = (g_ai_pid_start > 0) ? (time(NULL) - g_ai_pid_start) : 0;
                int delay = g_ai_restart_delay_sec;

                if (ran_sec >= 60) {
                    g_ai_fail_streak = 0;
                    delay = 3;
                } else {
                    g_ai_fail_streak++;
                }

                if (code == 42) {
                    delay = 15;
                } else if (code == 255 || code < 0) {
                    delay = 15;
                } else if (code == 0) {
                    delay = (ran_sec >= 30) ? 8 : 12;
                }

                if (g_ai_fail_streak >= 3) {
                    delay = 30;
                    g_ai_fail_streak = 0;
                    LOG("AI restart cooldown %ds after repeated failures", delay);
                } else {
                    LOG("AI process exited status=%d (ran=%lds), restart in %ds...",
                        code, (long)ran_sec, delay);
                }

                g_ai_pid = -1;
                g_ai_pid_start = 0;
                g_ai_restart_delay_sec = (delay < 30) ? (delay + 2) : 30;
                sleep((unsigned int)delay);

                if (g_stop_requested == 0 && widget_ai_use_attach()) {
                    if (camera_pipe_preview_active() != TD_TRUE) {
                        LOG("AI restart skipped: camera preview inactive");
                        g_ai_restart_delay_sec = 15;
                    } else if ((time(NULL) - g_ai_last_fork) < 5) {
                        LOG("AI restart skipped: too soon after last fork");
                    } else {
                        (void)widget_ai_fork_attach(TD_TRUE);
                    }
                }
            }
        }
        usleep(500000);
    }
    return NULL;
}

static td_void widget_ai_watch_start(td_void)
{
    if (g_ai_watch_run != 0) {
        return;
    }
    g_ai_watch_run = 1;
    if (pthread_create(&g_ai_watch_tid, NULL, widget_ai_watch_thread, NULL) != 0) {
        g_ai_watch_run = 0;
        LOG("AI watchdog thread create failed");
    }
}

static td_void widget_ai_watch_stop(td_void)
{
    if (g_ai_watch_run == 0) {
        return;
    }
    g_ai_watch_run = 0;
    (void)pthread_join(g_ai_watch_tid, NULL);
}

/* modelzoo 板端样例（与 /opt/sample/yolov8/sample_yolov8_os08a20 一致） */
static td_bool widget_ai_use_modelzoo(td_void)
{
    const char *backend = getenv("WIDGET_AI_BACKEND");

    if (backend != NULL) {
        if (strcmp(backend, "camera") == 0 || strcmp(backend, "none") == 0 ||
            strcmp(backend, "off") == 0) {
            return TD_FALSE;
        }
        if (strcmp(backend, "modelzoo") == 0 || strcmp(backend, "yolov8") == 0) {
            return TD_TRUE;
        }
    }
    return TD_FALSE;
}

/* 纯九轴挥拍时 NPU 留给 panel 内 IMU 1D CNN，勿再 fork sample_vio_ai attach */
static td_bool widget_hit_source_needs_camera_ai(td_void)
{
    const char *src = getenv("WIDGET_HIT_SOURCE");

    if (src == NULL || src[0] == '\0') {
        return TD_TRUE;
    }
    if (strcmp(src, "imu") == 0 || strcmp(src, "racket") == 0 || strcmp(src, "sle") == 0) {
        return TD_FALSE;
    }
    return TD_TRUE;
}

static td_bool widget_pose_overlay_wanted(td_void)
{
    const char *en = getenv("WIDGET_POSE_ENABLE");

    if (en != NULL && en[0] != '\0' && en[0] != '0') {
        return TD_TRUE;
    }
    return TD_FALSE;
}

/* camera_pipe + attach 推理画框（默认） */
static td_bool widget_ai_use_attach(td_void)
{
    const char *backend = getenv("WIDGET_AI_BACKEND");

    if (widget_ai_use_modelzoo()) {
        return TD_FALSE;
    }
    if (!widget_hit_source_needs_camera_ai() && !widget_pose_overlay_wanted()) {
        return TD_FALSE;
    }
    if (backend != NULL &&
        (strcmp(backend, "camera") == 0 || strcmp(backend, "none") == 0 || strcmp(backend, "off") == 0)) {
        return TD_FALSE;
    }
    return TD_TRUE;
}

static td_bool widget_ai_enabled(td_void)
{
    const char *dis = getenv("WIDGET_AI_DISABLE");

    if (dis != NULL && dis[0] != '\0' && dis[0] != '0') {
        return TD_FALSE;
    }
    if (widget_ai_use_modelzoo()) {
        return TD_TRUE;
    }
    if (widget_ai_use_attach()) {
        return TD_TRUE;
    }
    return camera_pipe_ai_channel_active();
}

static td_void widget_ai_stop(td_void)
{
    widget_ai_watch_stop();
    if (g_ai_pid > 0) {
        kill(g_ai_pid, SIGTERM);
        (void)waitpid(g_ai_pid, NULL, 0);
        g_ai_pid = -1;
        LOG("AI process stopped");
    }
    (void)system("killall sample_yolov8_os08a20 sample_vio_ai >/dev/null 2>&1");
}

static td_s32 widget_ai_start_attach(td_void)
{
    return widget_ai_fork_attach(TD_FALSE);
}

/* 等面板与 VO 小窗稳定后再 attach，避免 probe 干扰 VPSS 导致黑屏 */
static td_s32 widget_ai_start_attach_delayed(td_void)
{
    const char *model = getenv("WIDGET_AI_MODEL");
    const char *ai_bin = getenv("WIDGET_AI_BIN");
    const char *grp_env = getenv("WIDGET_AI_ATTACH_GRP");
    const char *chn_env = getenv("WIDGET_AI_ATTACH_CHN");
    const char *delay_env = getenv("WIDGET_AI_ATTACH_DELAY_SEC");
    char grp_buf[16];
    char chn_buf[16];
    const char *ld;
    char ld_buf[512];
    unsigned delay_sec = 10U;
    pid_t pid;

    if (!widget_ai_use_attach()) {
        return TD_SUCCESS;
    }

    if (model == NULL || model[0] == '\0') {
        model = DEFAULT_AI_MODEL;
    }
    if (ai_bin == NULL || ai_bin[0] == '\0') {
        ai_bin = DEFAULT_AI_ATTACH_BIN;
    }
    if (access(model, R_OK) != 0) {
        LOG("attach model not found: %s (skip)", model);
        return TD_SUCCESS;
    }
    if (access(ai_bin, X_OK) != 0) {
        LOG("attach binary not found: %s (skip)", ai_bin);
        return TD_SUCCESS;
    }
    if (delay_env != NULL && delay_env[0] != '\0') {
        delay_sec = (unsigned)strtoul(delay_env, NULL, 0);
        if (delay_sec > 60U) {
            delay_sec = 60U;
        }
    }

    (void)snprintf(grp_buf, sizeof(grp_buf), "%u",
        grp_env ? (td_u32)strtoul(grp_env, NULL, 0) : camera_pipe_vpss_grp());
    (void)snprintf(chn_buf, sizeof(chn_buf), "%u",
        chn_env ? (td_u32)strtoul(chn_env, NULL, 0) : camera_pipe_ai_vpss_chn());

    pid = fork();
    if (pid < 0) {
        LOG("fork delayed attach AI failed: %m");
        return TD_FAILURE;
    }
    if (pid == 0) {
        sleep((unsigned int)delay_sec);
        ld = getenv("LD_LIBRARY_PATH");
        if (ld != NULL && ld[0] != '\0') {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:%s", ld);
        } else {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:/usr/lib/aarch64-linux-gnu");
        }
        setenv("LD_LIBRARY_PATH", ld_buf, 1);
        setenv("ASCEND_AICPU_KERNEL_PATH", "/opt/lib/npu", 1);
        if (freopen(AI_ATTACH_LOG_PATH, "w", stdout) == NULL) {
            /* continue */
        }
        if (freopen(AI_ATTACH_LOG_PATH, "a", stderr) == NULL) {
            /* continue */
        }
        execl(ai_bin, ai_bin, model, "attach", grp_buf, chn_buf, (char *)NULL);
        LOG("execl delayed attach AI failed: %m");
        _exit(127);
    }

    g_ai_pid = pid;
    g_ai_pid_start = time(NULL);
    g_ai_last_fork = g_ai_pid_start;
    LOG("YOLO attach scheduled in %us pid=%d: %s %s attach %s %s",
        delay_sec, (int)pid, ai_bin, model, grp_buf, chn_buf);
    return TD_SUCCESS;
}

static td_s32 widget_ai_prepare_modelzoo_om(const char *model, const char *workdir)
{
    char dst[256];
    char cmd[640];

    if (model == NULL || model[0] == '\0') {
        model = DEFAULT_AI_MODEL;
    }
    if (workdir == NULL || workdir[0] == '\0') {
        workdir = DEFAULT_YOLOV8_WORKDIR;
    }
    (void)snprintf(dst, sizeof(dst), "%s/%s", workdir, DEFAULT_YOLOV8_OM_NAME);
    (void)snprintf(cmd, sizeof(cmd), "cp -f '%s' '%s'", model, dst);
    if (system(cmd) != 0) {
        LOG("prepare model failed: %s -> %s", model, dst);
        return TD_FAILURE;
    }
    LOG("modelzoo om ready: %s", dst);
    return TD_SUCCESS;
}

static td_s32 widget_ai_start_modelzoo(td_void)
{
    const char *model = getenv("WIDGET_AI_MODEL");
    const char *ai_bin = getenv("WIDGET_AI_BIN");
    const char *workdir = getenv("WIDGET_AI_WORKDIR");
    const char *ld;
    char ld_buf[512];
    pid_t pid;

    if (!widget_ai_enabled()) {
        LOG("AI disabled (WIDGET_AI_DISABLE=1)");
        return TD_SUCCESS;
    }

    if (model == NULL || model[0] == '\0') {
        model = DEFAULT_AI_MODEL;
    }
    if (ai_bin == NULL || ai_bin[0] == '\0') {
        ai_bin = DEFAULT_YOLOV8_BIN;
    }
    if (workdir == NULL || workdir[0] == '\0') {
        workdir = DEFAULT_YOLOV8_WORKDIR;
    }

    if (access(model, R_OK) != 0) {
        LOG("AI model not found: %s (skip modelzoo)", model);
        return TD_SUCCESS;
    }
    if (access(ai_bin, X_OK) != 0) {
        LOG("modelzoo binary not found: %s (skip)", ai_bin);
        return TD_SUCCESS;
    }
    if (access(workdir, R_OK | X_OK) != 0) {
        LOG("modelzoo workdir not accessible: %s (skip)", workdir);
        return TD_SUCCESS;
    }

    if (widget_ai_prepare_modelzoo_om(model, workdir) != TD_SUCCESS) {
        return TD_FAILURE;
    }

    pid = fork();
    if (pid < 0) {
        LOG("fork modelzoo failed: %m");
        return TD_FAILURE;
    }
    if (pid == 0) {
        ld = getenv("LD_LIBRARY_PATH");
        if (ld != NULL && ld[0] != '\0') {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:%s", ld);
        } else {
            (void)snprintf(ld_buf, sizeof(ld_buf), "/opt/lib/npu:/opt/lib:/usr/lib/aarch64-linux-gnu");
        }
        setenv("LD_LIBRARY_PATH", ld_buf, 1);
        setenv("ASCEND_AICPU_KERNEL_PATH", "/opt/lib/npu", 1);
        if (chdir(workdir) != 0) {
            LOG("chdir %s failed: %m", workdir);
            _exit(127);
        }
        if (freopen(AI_MODELZOO_LOG_PATH, "w", stdout) == NULL) {
            /* continue */
        }
        if (freopen(AI_MODELZOO_LOG_PATH, "a", stderr) == NULL) {
            /* continue */
        }
        execl(ai_bin, ai_bin, (char *)NULL);
        LOG("execl modelzoo failed: %m");
        _exit(127);
    }

    g_ai_pid = pid;
    LOG("modelzoo YOLOv8 started pid=%d: cd %s && %s (om=%s/%s)",
        (int)pid, workdir, ai_bin, workdir, DEFAULT_YOLOV8_OM_NAME);
    sleep(2);
    return TD_SUCCESS;
}

static td_s32 launch_panel(td_void)
{
    pid_t pid;
    td_s32 status;

    pid = fork();
    if (pid < 0) {
        LOG("fork failed: %m");
        return TD_FAILURE;
    }

    if (pid == 0) {
        const char *env_touch;
        const char *env_font;

        setenv("QT_QPA_PLATFORM", "linuxfb:fb=/dev/fb0:size=1920x1080", 1);
        /* 与系统 Qt5.15 + libqt5multimedia5-plugins 一致；/opt/lib 仅保留给其它海思库依赖 */
        setenv("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms", 1);
        setenv("QT_PLUGIN_PATH", "/usr/lib/aarch64-linux-gnu/qt5/plugins", 1);
        setenv("QT_QPA_FB_HIDECURSOR", "1", 1);
        env_touch = getenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS");
        if (env_touch != NULL && env_touch[0] != '\0') {
            setenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", env_touch, 1);
        }
        env_font = getenv("QT_QPA_FONTDIR");
        if (env_font != NULL && env_font[0] != '\0') {
            setenv("QT_QPA_FONTDIR", env_font, 1);
        }
        /* Qt 平台插件优先用系统路径；/opt/lib/npu 供 libascendcl 依赖（libmsprofiler 等） */
        setenv("LD_LIBRARY_PATH", "/usr/lib/aarch64-linux-gnu:/opt/lib/npu:/opt/lib", 1);
        (void)freopen(PANEL_LOG_PATH, "a", stdout);
        (void)freopen(PANEL_LOG_PATH, "a", stderr);

        execl(PANEL_PATH, PANEL_PATH, (char *)NULL);
        LOG("execl panel failed: %m");
        _exit(1);
    }

    LOG("panel PID=%d started", pid);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            LOG("waitpid failed: %m");
            return TD_FAILURE;
        }
    }

    if (WIFEXITED(status)) {
        LOG("panel exited, status=%d", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        LOG("panel killed by signal %d", WTERMSIG(status));
    }

    return TD_SUCCESS;
}

static td_void vo_gfbg_parse_cam_mipi(int argc, char *argv[], td_u32 *out_mipi)
{
    int i;
    const char *eq;
    const char *ev;

    *out_mipi = (td_u32)CAMERA_PIPE_DEFAULT_MIPI_SOCKET;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--mipi=", (size_t)7) == 0) {
            *out_mipi = (td_u32)strtoul(argv[i] + 7, NULL, 0);
            return;
        }
        if (strcmp(argv[i], "--mipi") == 0 && i + 1 < argc) {
            *out_mipi = (td_u32)strtoul(argv[i + 1], NULL, 0);
            return;
        }
    }

    ev = getenv("WIDGET_CAM_MIPI");
    if (ev != NULL && ev[0] != '\0') {
        *out_mipi = (td_u32)strtoul(ev, NULL, 0);
        return;
    }

    fprintf(stderr,
        "[vo_gfbg] 默认路 %u（EULER_2R+OS08A20 对齐 sample_vio %u 0）：socket0=J3 i2c5；socket1=J4 VI dev2+i2c7；仅第二插座用 --mipi=2；覆盖总线 WIDGET_CAM_I2C_BUS / WIDGET_CAM_SENSOR1_BUS\n",
        (unsigned)CAMERA_PIPE_DEFAULT_MIPI_SOCKET, (unsigned)CAMERA_PIPE_DEFAULT_MIPI_SOCKET);

    /* 勿把固件 sample_vio 的 index 当本程序裸参数：HiEuerPI 等固件上常为
     *   sample_vio <index> <venc_en> —— (0)sensor0 第一路、(1)sensor1 第二路、(2)双路；
     * 与 SDK 源码树里「十几种用例」的编号不是同一套。对应关系：固件 index1 ≈ 本程序 --mipi=1。 */
    if (argc >= 2 && argv[1][0] != '-') {
        eq = argv[1];
        if ((eq[0] >= '0' && eq[0] <= '2') && eq[1] == '\0') {
            fprintf(stderr,
                "[vo_gfbg] 忽略位置参数 \"%s\"：请用 WIDGET_CAM_MIPI=0..2 或 --mipi=0..2（避免与 sample_vio 用例号混淆）\n",
                argv[1]);
        }
    }
}

int main(int argc, char *argv[])
{
    td_s32 ret;
    td_u32 mipi_sock = 0;

    signal(SIGINT, vo_gfbg_on_stop_signal);
    signal(SIGTERM, vo_gfbg_on_stop_signal);
    {
        FILE *pl = fopen(PANEL_LOG_PATH, "we");
        if (pl != NULL) {
            (void)fclose(pl);
        }
    }
    vo_gfbg_reset_camvo_file();

    vo_gfbg_parse_cam_mipi(argc, argv, &mipi_sock);
    if (mipi_sock > 2U) {
        mipi_sock = 2U;
    }
    camera_pipe_set_mipi_socket(mipi_sock);

    {
        td_bool use_modelzoo = widget_ai_use_modelzoo() && widget_ai_enabled();

        LOG("=== SS928 panel launcher (v3.0 GFBG + %s) ===",
            use_modelzoo ? "modelzoo YOLOv8" : "camera_pipe");

        if (use_modelzoo) {
            LOG("WIDGET_AI_BACKEND=modelzoo: camera_pipe 跳过（样例独占 VI/VPSS/VO）");
            ret = widget_ai_start_modelzoo();
            if (ret != TD_SUCCESS) {
                LOG("modelzoo start failed (continuing without AI)");
            }
        } else {
            ret = camera_pipe_mpp_prepare();
            if (ret != TD_SUCCESS) {
                LOG("camera_pipe_mpp_prepare failed");
                return 1;
            }

            ret = camera_pipe_preview_start();
            if (ret != TD_SUCCESS) {
                LOG("camera_pipe_preview_start failed");
                camera_pipe_shutdown();
                return 1;
            }
            camvo_thread_start();

            ret = gfbg_init();
            if (ret != TD_SUCCESS) {
                LOG("GFBG init failed");
                camvo_thread_stop();
                camera_pipe_shutdown();
                return 1;
            }

            /* AI attach 延迟启动（对齐备份版）；camvo 线程在用户打开摄像头页时也会补启 */
            ret = widget_ai_start_attach_delayed();
            if (ret != TD_SUCCESS) {
                LOG("widget_ai_start_attach_delayed failed (preview only)");
            }
            widget_ai_watch_start();

            LOG("%s + GFBG ready, launching panel...", "camera");
            while (g_stop_requested == 0) {
                (void)launch_panel();
                if (g_stop_requested != 0) {
                    break;
                }
                LOG("panel exited, restart in 2s (camera preview stays on)");
                sleep(2);
            }
        }

        if (!use_modelzoo) {
            camvo_thread_stop();
        }
        widget_ai_stop();

        LOG("cleaning up...");
        if (!use_modelzoo) {
            camera_pipe_shutdown();
        }
    }

    LOG("=== exit ===");
    return 0;
}
