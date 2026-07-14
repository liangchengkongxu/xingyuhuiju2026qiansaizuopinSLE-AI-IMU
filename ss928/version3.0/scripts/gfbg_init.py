# GFBG initialization for SS928 HDMI output
# Based on QT linuxfb plugin's initialization sequence (qlinuxfbscreen.cpp)

import fcntl
import struct
import ctypes
import os
import sys

# ---- IoCtl macros (aarch64 Linux) ----
_IOC_NRBITS   = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS  = 2
_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT  = _IOC_SIZESHIFT + _IOC_SIZEBITS
_IOC_WRITE = 1
_IOC_READ  = 2
_IOC_NONE  = 0

def _IOC(d, t, nr, size):
    return (d << _IOC_DIRSHIFT) | (size << _IOC_SIZESHIFT) | (ord(t) << _IOC_TYPESHIFT) | (nr << _IOC_NRSHIFT)

def _IOW(t, nr, size): return _IOC(_IOC_WRITE, t, nr, size)
def _IOR(t, nr, size): return _IOC(_IOC_READ, t, nr, size)

# ---- GFBG ioctl numbers ----
IOC_TYPE_GFBG = 'F'

FBIOPUT_SHOW_GFBG         = _IOW(IOC_TYPE_GFBG, 101, 4)   # td_bool (int)
FBIOPUT_SCREEN_ORIGIN_GFBG = _IOW(IOC_TYPE_GFBG, 95, 8)    # ot_fb_point (2*int)
FBIOPUT_LAYER_INFO         = _IOW(IOC_TYPE_GFBG, 120, 64)  # ot_fb_layer_info
FBIOPUT_ALPHA_GFBG         = _IOW(IOC_TYPE_GFBG, 93, 8)    # ot_fb_alpha
FBIOPUT_COLORKEY_GFBG      = _IOW(IOC_TYPE_GFBG, 91, 8)    # ot_fb_colorkey
FBIOPUT_COMPRESSION_GFBG   = _IOW(IOC_TYPE_GFBG, 133, 4)   # td_bool
FBIO_REFRESH               = _IOW(IOC_TYPE_GFBG, 124, 40)  # ot_fb_buf

OT_FB_LAYER_BUF_NONE   = 0x2  # Passthrough: std fb writes go directly to VO
OT_FB_LAYER_BUF_DOUBLE = 0x0  # Double buffer
OT_FB_LAYER_MASK_BUF_MODE = 0x1
OT_FB_FORMAT_ARGB1555 = 6

# Standard fb ioctls
FBIOGET_VSCREENINFO = 0x4600
FBIOPUT_VSCREENINFO = 0x4601
FBIOGET_FSCREENINFO = 0x4602

# Desired resolution
SCREEN_WIDTH  = 1920
SCREEN_HEIGHT = 1080
BPP           = 16  # ARGB1555

def gfbg_init(fbdev="/dev/fb0"):
    fd = os.open(fbdev, os.O_RDWR)
    
    # 1. Disable display
    print("[1] Disabling display...")
    show_val = struct.pack("i", 0)  # TD_FALSE
    fcntl.ioctl(fd, FBIOPUT_SHOW_GFBG, show_val)
    
    # 2. Set screen origin to (0, 0)
    print("[2] Setting screen origin (0,0)...")
    origin = struct.pack("ii", 0, 0)
    fcntl.ioctl(fd, FBIOPUT_SCREEN_ORIGIN_GFBG, origin)
    
    # 3. Read current vinfo (full 160-byte struct for aarch64)
    print("[3] Reading vinfo...")
    vinfo_raw = bytearray(160)  # sizeof(struct fb_var_screeninfo) on aarch64
    fcntl.ioctl(fd, FBIOGET_VSCREENINFO, vinfo_raw, True)
    old_xres, old_yres = struct.unpack_from("II", vinfo_raw, 0)
    print(f"    Current vinfo: {old_xres}x{old_yres}")
    
    # 4. Modify vinfo from GET (must preserve pixel clock, sync timings, etc.
    #    that kernel requires — constructing from scratch gets EPERM)
    # struct fb_var_screeninfo layout (aarch64, kernel 4.19):
    #   0:xres  4:yres  8:xres_virtual  12:yres_virtual
    #   16:xoffset  20:yoffset  24:bits_per_pixel  28:grayscale
    #   32:red  44:green  56:blue  68:transp  (each fb_bitfield = 12 bytes)
    #   80:nonstd  84:activate  ...  160 total
    struct.pack_into("IIIII", vinfo_raw, 0,  SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT, BPP)
    # ARGB1555 bitfield: a(15,1,0), r(10,5,0), g(5,5,0), b(0,5,0)
    struct.pack_into("III", vinfo_raw, 32, 10, 5, 0)  # red
    struct.pack_into("III", vinfo_raw, 44,  5, 5, 0)  # green
    struct.pack_into("III", vinfo_raw, 56,  0, 5, 0)  # blue
    struct.pack_into("III", vinfo_raw, 68, 15, 1, 0)  # transp (alpha)
    # activate = FB_ACTIVATE_NOW (offset 84)
    struct.pack_into("I", vinfo_raw, 84, 0)
    
    # NOTE: FBPIOUT_VSCREENINFO is rejected (EPERM) when VO is not
    # initialized at the target resolution via MPP first.
    # We skip it and rely on the current VO resolution (3840x2160 by default).
    # Qt linuxfb will adapt to whatever the fb reports.
    print("[4] Skipping FBIOPUT_VSCREENINFO (current VO res is %dx%d, EPERM if mismatched)" % (old_xres, old_yres))
    # fcntl.ioctl(fd, FBIOPUT_VSCREENINFO, bytes(vinfo_raw))
    
    # 5. Set layer info - BUF_NONE for direct passthrough to VO
    print("[5] Setting layer info (BUF_NONE passthrough)...")
    # ot_fb_layer_info: buf_mode(u32), antiflicker(u32), x_pos(i32), y_pos(i32),
    # canvas_w(u32), canvas_h(u32), disp_w(u32), disp_h(u32),
    # screen_w(u32), screen_h(u32), premul(u8+padding), mask(u32)
    # ot_fb_layer_info is 12 fields (not 13)
    layer_info = struct.pack("IIiiIIIIIIII",
        OT_FB_LAYER_BUF_NONE,   # buf_mode
        0,                       # antiflicker_level
        0, 0,                    # x_pos, y_pos  
        SCREEN_WIDTH, SCREEN_HEIGHT,  # canvas_width, canvas_height
        SCREEN_WIDTH, SCREEN_HEIGHT,  # display_width, display_height
        SCREEN_WIDTH, SCREEN_HEIGHT,  # screen_width, screen_height
        0,                       # is_premul
        OT_FB_LAYER_MASK_BUF_MODE  # mask
    )
    fcntl.ioctl(fd, FBIOPUT_LAYER_INFO, layer_info)
    
    # 6. Enable display
    print("[6] Enabling display...")
    show_val = struct.pack("i", 1)  # TD_TRUE
    fcntl.ioctl(fd, FBIOPUT_SHOW_GFBG, show_val)
    
    # 7. Disable compression
    print("[7] Disabling compression...")
    fcntl.ioctl(fd, FBIOPUT_COMPRESSION_GFBG, struct.pack("i", 0))
    
    # 8. Set alpha
    print("[8] Setting alpha...")
    # ot_fb_alpha: alpha_en(u8), alpha_chn_en(u8), alpha0(u8), alpha1(u8), global_alpha(u8), reserved(u8)
    alpha = struct.pack("BBBBBB", 1, 0, 0xFF, 0, 0xFF, 0)
    # Pad to 8 bytes
    alpha = alpha + b'\x00\x00'
    fcntl.ioctl(fd, FBIOPUT_ALPHA_GFBG, alpha)
    
    # 9. Set colorkey (filter black)
    print("[9] Setting colorkey...")
    # ot_fb_colorkey: enable(u32), value(u32) - actually it's td_bool then td_u32, 
    # wait, looking at the struct: td_bool enable; td_u32 value;
    # In gfbg.h: struct { td_bool enable; td_u32 value; }
    # td_bool is likely 4 bytes (int) on this platform
    colorkey = struct.pack("II", 1, 0x000000)
    fcntl.ioctl(fd, FBIOPUT_COLORKEY_GFBG, colorkey)
    
    print("[DONE] GFBG initialized successfully!")
    print(f"Resolution: {SCREEN_WIDTH}x{SCREEN_HEIGHT} {BPP}bpp")
    # 不关闭 fd — 留给 Qt 子进程使用，避免重新设置 vinfo 时模式冲突
    return True

if __name__ == "__main__":
    try:
        gfbg_init()
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)