#include "vio_ai_internal.h"

td_bool vio_ai_pixel_format_is_nv21(td_u32 pixel_format)
{
#ifdef OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
    if (pixel_format == (td_u32)OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420) {
        return TD_TRUE;
    }
#endif
    return TD_FALSE;
}
void resize_yuv420sp_nn(const unsigned char *src, td_u32 src_w, td_u32 src_h, td_u32 src_stride_y, td_u32 src_stride_uv,
    unsigned char *dst, td_u32 dst_w, td_u32 dst_h, td_bool src_is_nv21, td_bool dst_is_nv21)
{
    td_u32 y;
    td_u32 x;
    unsigned char *dst_y = dst;
    unsigned char *dst_uv = dst + (size_t)dst_w * dst_h;
    const unsigned char *src_y = src;
    const unsigned char *src_uv = src + (size_t)src_stride_y * src_h;

    for (y = 0; y < dst_h; y++) {
        td_u32 sy = (td_u32)(((uint64_t)y * src_h) / dst_h);
        const unsigned char *src_row = src_y + (size_t)sy * src_stride_y;
        unsigned char *dst_row = dst_y + (size_t)y * dst_w;
        for (x = 0; x < dst_w; x++) {
            td_u32 sx = (td_u32)(((uint64_t)x * src_w) / dst_w);
            dst_row[x] = src_row[sx];
        }
    }

    for (y = 0; y < dst_h / 2; y++) {
        td_u32 sy = (td_u32)(((uint64_t)y * (src_h / 2)) / (dst_h / 2));
        const unsigned char *src_row = src_uv + (size_t)sy * src_stride_uv;
        unsigned char *dst_row = dst_uv + (size_t)y * dst_w;
        for (x = 0; x < dst_w; x += 2) {
            td_u32 sx = (td_u32)(((uint64_t)x * src_w) / dst_w);
            unsigned char su;
            unsigned char sv;
            sx &= ~1U;
            if (sx + 1 >= src_w) {
                sx = (src_w >= 2) ? (src_w - 2) : 0;
            }
            if (src_is_nv21 == TD_TRUE) {
                sv = src_row[sx];
                su = src_row[sx + 1];
            } else {
                su = src_row[sx];
                sv = src_row[sx + 1];
            }
            if (dst_is_nv21 == TD_TRUE) {
                dst_row[x] = sv;
                dst_row[x + 1] = su;
            } else {
                dst_row[x] = su;
                dst_row[x + 1] = sv;
            }
        }
    }
}

static unsigned char bilerp_u8(unsigned char p00, unsigned char p01, unsigned char p10, unsigned char p11, unsigned int fx, unsigned int fy)
{
    /* fx,fy in [0,255] */
    unsigned int w00 = (255U - fx) * (255U - fy);
    unsigned int w01 = fx * (255U - fy);
    unsigned int w10 = (255U - fx) * fy;
    unsigned int w11 = fx * fy;
    unsigned int v = w00 * p00 + w01 * p01 + w10 * p10 + w11 * p11;
    v = (v + (255U * 255U / 2U)) / (255U * 255U);
    if (v > 255U) v = 255U;
    return (unsigned char)v;
}

static void resize_y_plane_bilinear(const unsigned char *src_y, td_u32 src_w, td_u32 src_h, td_u32 src_stride,
    unsigned char *dst_y, td_u32 dst_w, td_u32 dst_h)
{
    td_u32 y;
    td_u32 x;
    unsigned int scale_x = (dst_w > 1) ? ((src_w - 1U) << 8) / (dst_w - 1U) : 0;
    unsigned int scale_y = (dst_h > 1) ? ((src_h - 1U) << 8) / (dst_h - 1U) : 0;

    for (y = 0; y < dst_h; y++) {
        unsigned int sy8 = y * scale_y;
        td_u32 sy = (td_u32)(sy8 >> 8);
        unsigned int fy = sy8 & 0xffU;
        td_u32 sy1 = (sy + 1U < src_h) ? (sy + 1U) : sy;
        const unsigned char *row0 = src_y + (size_t)sy * src_stride;
        const unsigned char *row1 = src_y + (size_t)sy1 * src_stride;
        unsigned char *drow = dst_y + (size_t)y * dst_w;
        for (x = 0; x < dst_w; x++) {
            unsigned int sx8 = x * scale_x;
            td_u32 sx = (td_u32)(sx8 >> 8);
            unsigned int fx = sx8 & 0xffU;
            td_u32 sx1 = (sx + 1U < src_w) ? (sx + 1U) : sx;
            drow[x] = bilerp_u8(row0[sx], row0[sx1], row1[sx], row1[sx1], fx, fy);
        }
    }
}

static void resize_uv_bilinear(const unsigned char *src_uv, td_u32 src_w, td_u32 src_h, td_u32 src_stride,
    unsigned char *dst_uv, td_u32 dst_w, td_u32 dst_h, td_bool src_is_nv21, td_bool dst_is_nv21)
{
    /* UV plane has size (w x h/2), but chroma samples are on grid (w/2 x h/2) */
    td_u32 dst_ch = dst_h / 2;
    td_u32 src_ch = src_h / 2;
    td_u32 x;
    td_u32 y;
    td_u32 src_cw = (src_w / 2);
    td_u32 dst_cw = (dst_w / 2);
    unsigned int scale_x = (dst_cw > 1) ? ((src_cw - 1U) << 8) / (dst_cw - 1U) : 0;
    unsigned int scale_y = (dst_ch > 1) ? ((src_ch - 1U) << 8) / (dst_ch - 1U) : 0;

    for (y = 0; y < dst_ch; y++) {
        unsigned int sy8 = y * scale_y;
        td_u32 sy = (td_u32)(sy8 >> 8);
        unsigned int fy = sy8 & 0xffU;
        td_u32 sy1 = (sy + 1U < src_ch) ? (sy + 1U) : sy;
        const unsigned char *row0 = src_uv + (size_t)sy * src_stride;
        const unsigned char *row1 = src_uv + (size_t)sy1 * src_stride;
        unsigned char *drow = dst_uv + (size_t)y * dst_w;
        for (x = 0; x < dst_cw; x++) {
            unsigned int sx8 = x * scale_x;
            td_u32 sx = (td_u32)(sx8 >> 8);
            unsigned int fx = sx8 & 0xffU;
            td_u32 sx1 = (sx + 1U < src_cw) ? (sx + 1U) : sx;

            /* Read u/v at chroma sample position (sx, sy) -> byte offset 2*sx */
            td_u32 o00 = (sx << 1);
            td_u32 o01 = (sx1 << 1);
            unsigned char u00, v00, u01, v01, u10, v10, u11, v11;
            if (src_is_nv21 == TD_TRUE) {
                v00 = row0[o00]; u00 = row0[o00 + 1];
                v01 = row0[o01]; u01 = row0[o01 + 1];
                v10 = row1[o00]; u10 = row1[o00 + 1];
                v11 = row1[o01]; u11 = row1[o01 + 1];
            } else {
                u00 = row0[o00]; v00 = row0[o00 + 1];
                u01 = row0[o01]; v01 = row0[o01 + 1];
                u10 = row1[o00]; v10 = row1[o00 + 1];
                u11 = row1[o01]; v11 = row1[o01 + 1];
            }

            {
                unsigned char u = bilerp_u8(u00, u01, u10, u11, fx, fy);
                unsigned char v = bilerp_u8(v00, v01, v10, v11, fx, fy);
                td_u32 doff = (x << 1);
                if (dst_is_nv21 == TD_TRUE) {
                    drow[doff] = v;
                    drow[doff + 1] = u;
                } else {
                    drow[doff] = u;
                    drow[doff + 1] = v;
                }
            }
        }
    }
}

void vio_ai_resize_yuv420sp_bilinear(const unsigned char *src, td_u32 src_w, td_u32 src_h, td_u32 src_stride_y, td_u32 src_stride_uv,
    unsigned char *dst, td_u32 dst_w, td_u32 dst_h, td_bool src_is_nv21, td_bool dst_is_nv21)
{
    unsigned char *dst_y = dst;
    unsigned char *dst_uv = dst + (size_t)dst_w * dst_h;
    const unsigned char *src_y = src;
    const unsigned char *src_uv = src + (size_t)src_stride_y * src_h;
    resize_y_plane_bilinear(src_y, src_w, src_h, src_stride_y, dst_y, dst_w, dst_h);
    resize_uv_bilinear(src_uv, src_w, src_h, src_stride_uv, dst_uv, dst_w, dst_h, src_is_nv21, dst_is_nv21);
}

static void fill_nv21(unsigned char *dst, td_u32 w, td_u32 h, unsigned char yv, unsigned char uv)
{
    size_t y_sz = (size_t)w * h;
    size_t uv_sz = y_sz / 2;
    (void)memset(dst, yv, y_sz);
    (void)memset(dst + y_sz, uv, uv_sz);
}

td_bool vio_ai_resize_yuv420sp_letterbox(const unsigned char *src, td_u32 src_w, td_u32 src_h, td_u32 src_stride_y, td_u32 src_stride_uv,
    unsigned char *dst, td_u32 dst_w, td_u32 dst_h, td_bool src_is_nv21, td_bool dst_is_nv21)
{
    td_u32 scaled_w;
    td_u32 scaled_h;
    td_u32 off_x;
    td_u32 off_y;
    unsigned char *tmp = TD_NULL;
    td_u32 y;

    if ((src == TD_NULL) || (dst == TD_NULL) || (src_w == 0) || (src_h == 0) || (dst_w == 0) || (dst_h == 0)) {
        return TD_FALSE;
    }

    {
        float r_w = (float)dst_w / (float)src_w;
        float r_h = (float)dst_h / (float)src_h;
        float r = (r_w < r_h) ? r_w : r_h;
        if (r <= 0.0f) {
            return TD_FALSE;
        }
        scaled_w = (td_u32)lroundf((double)((float)src_w * r));
        scaled_h = (td_u32)lroundf((double)((float)src_h * r));
    }
    if (scaled_w < 2) scaled_w = 2;
    if (scaled_h < 2) scaled_h = 2;
    scaled_w &= ~1U;
    scaled_h &= ~1U;
    if (scaled_w > dst_w) scaled_w = dst_w & ~1U;
    if (scaled_h > dst_h) scaled_h = dst_h & ~1U;

    off_x = (dst_w - scaled_w) / 2;
    off_y = (dst_h - scaled_h) / 2;
    off_x &= ~1U;
    off_y &= ~1U;

    fill_nv21(dst, dst_w, dst_h, 114, 128);

    tmp = (unsigned char *)malloc((size_t)scaled_w * scaled_h * 3 / 2);
    if (tmp == TD_NULL) {
        return TD_FALSE;
    }

    vio_ai_resize_yuv420sp_bilinear(src, src_w, src_h, src_stride_y, src_stride_uv, tmp, scaled_w, scaled_h, src_is_nv21, dst_is_nv21);

    for (y = 0; y < scaled_h; y++) {
        unsigned char *dst_row = dst + (size_t)(off_y + y) * dst_w + off_x;
        const unsigned char *src_row = tmp + (size_t)y * scaled_w;
        (void)memcpy(dst_row, src_row, scaled_w);
    }
    for (y = 0; y < scaled_h / 2; y++) {
        unsigned char *dst_row = dst + (size_t)dst_w * dst_h + (size_t)(off_y / 2 + y) * dst_w + off_x;
        const unsigned char *src_row = tmp + (size_t)scaled_w * scaled_h + (size_t)y * scaled_w;
        (void)memcpy(dst_row, src_row, scaled_w);
    }

    free(tmp);
    return TD_TRUE;
}
void vio_ai_draw_box_y_plane(unsigned char *y, td_u32 stride, td_u32 img_w, td_u32 img_h,
    int x1, int y1, int x2, int y2, unsigned char v, int thick)
{
    int x;
    int yy;
    int t;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= (int)img_w) x2 = (int)img_w - 1;
    if (y2 >= (int)img_h) y2 = (int)img_h - 1;
    if ((x2 <= x1) || (y2 <= y1)) {
        return;
    }
    if (thick < 1) {
        thick = 1;
    }
    for (t = 0; t < thick; t++) {
        int ty1 = y1 + t;
        int ty2 = y2 - t;
        int tx1 = x1 + t;
        int tx2 = x2 - t;
        if ((ty1 >= (int)img_h) || (ty2 < 0) || (tx1 >= (int)img_w) || (tx2 < 0)) {
            break;
        }
        if (ty1 < 0) ty1 = 0;
        if (ty2 >= (int)img_h) ty2 = (int)img_h - 1;
        if (tx1 < 0) tx1 = 0;
        if (tx2 >= (int)img_w) tx2 = (int)img_w - 1;
        for (x = tx1; x <= tx2; x++) {
            y[ty1 * stride + x] = v;
            y[ty2 * stride + x] = v;
        }
        for (yy = ty1; yy <= ty2; yy++) {
            y[yy * stride + tx1] = v;
            y[yy * stride + tx2] = v;
        }
    }
}

void vio_ai_draw_line_y_plane(unsigned char *y, td_u32 stride, td_u32 img_w, td_u32 img_h,
    int x0, int y0, int x1, int y1, unsigned char v, int thick)
{
    int dx;
    int dy;
    int steps;
    int i;
    int t;
    int half;

    if (y == TD_NULL || img_w == 0 || img_h == 0) {
        return;
    }
    if (thick < 1) {
        thick = 1;
    }
    dx = x1 - x0;
    dy = y1 - y0;
    steps = (dx >= 0 ? dx : -dx);
    if ((dy >= 0 ? dy : -dy) > steps) {
        steps = (dy >= 0 ? dy : -dy);
    }
    if (steps <= 0) {
        steps = 1;
    }
    half = thick / 2;
    for (i = 0; i <= steps; i++) {
        int px = x0 + (dx * i) / steps;
        int py = y0 + (dy * i) / steps;
        for (t = -half; t <= half; t++) {
            int xx = px + t;
            int yy = py + t;
            if (xx >= 0 && yy >= 0 && (td_u32)xx < img_w && (td_u32)yy < img_h) {
                y[(td_u32)yy * stride + (td_u32)xx] = v;
            }
        }
    }
}
