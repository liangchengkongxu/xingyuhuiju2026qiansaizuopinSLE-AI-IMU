# SS928 GFBG + Qt linuxfb 面板调试经验总结

## 现象

Qt Widgets 面板 (`widget_panel`) 在 SS928 板上通过 `vo_gfbg_init` 启动后，
**面板窗口短暂显示（约 0.5 秒）随即退出**，子进程退出状态码 `status=1`。

即：不是"完全无法渲染"，而是"渲染一瞬间后 Qt 主动退出"。

## 根因

**GFBG 使用了 BUF_DOUBLE（双缓冲模式），与 Qt linuxfb plugin 的 pan display 初始化逻辑发生冲突。**

具体触发链路：

1. 父进程通过 ioctl 设置 GFBG 为 `OT_FB_LAYER_BUF_DOUBLE`，`yres_virtual = 1080*2 = 2160`
2. 父进程 `close(fb_fd)` 关闭 `/dev/fb0`
3. 子进程（Qt）启动时重新 `open("/dev/fb0")`
4. Qt linuxfb plugin 调用 `FBIOGET_VSCREENINFO` 读到 `yres_virtual=2160`
5. Qt 尝试用 `FBIOPUT_VSCREENINFO` 把自己期望的 1920x1080 写回去
6. **SS928 内核驱动在 GFBG 模式下拒绝将 `yres_virtual` 从 2160 改回 1080**（双缓冲要求 virtual ≥ 2*res）
7. `FBIOPUT_VSCREENINFO` 返回错误
8. Qt 初始化 fb 失败 → `exit(1)`

## 解决方案（4 个关键修改）

### 1. BUF_DOUBLE → BUF_NONE（核心）

```c
// 错误（双缓冲）：
layer_info.buf_mode = OT_FB_LAYER_BUF_DOUBLE;
var.yres_virtual = 1080 * 2;

// 正确（直通模式）：
layer_info.buf_mode = OT_FB_LAYER_BUF_NONE;
var.yres_virtual = 1080;
```

SS928 GFBG 的 BUF_NONE 模式直接将用户态 fb 写入映射到 VO 硬件输出，
不需要 pan display 切换 buffer，与 Qt 的简单 fb 写入模型兼容。

### 2. 父进程不关闭 fb_fd

```c
// 错误：
close(fb_fd);  // 子进程 reopen 时重新设置 vinfo 导致冲突

// 正确：
// 不关闭，留给子进程继承使用
```

Qt 直接使用父进程已设置好的 fd（或 fork 后的副本），
**避免第二次 FBIOPUT_VSCREENINFO 调用**，从而绕过了驱动层的尺寸校验冲突。

### 3. yres_virtual = yres（单缓冲）

```c
var.yres_virtual = 1080;      // 而非 1080*2
var.xres_virtual = 1920;      // 而非 1920*2
```

必须与 BUF_NONE 保持一致，virtual 与 actual 尺寸相同。

### 4. 隐藏 Qt 光标

```c
setenv("QT_QPA_FB_HIDECURSOR", "1", 1);
```

防止 Qt 尝试在 fb 上通过 sprite ioctl 绘制鼠标光标，
某些 SS928 内核版本不支持这些 ioctl，会导致额外错误。

## GFBG 初始化正确顺序

```c
// 1. 关闭显示
enable = TD_FALSE;
ioctl(fb_fd, FBIOPUT_SHOW_GFBG, &enable);

// 2. 设置 vinfo（必须在 PUT_LAYER_INFO 之前）
ioctl(fb_fd, FBIOGET_VSCREENINFO, &var);
var.xres = var.xres_virtual = 1920;
var.yres = var.yres_virtual = 1080;    // ← virtual == actual
var.bits_per_pixel = 16;
var.activate = FB_ACTIVATE_NOW;
ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var);

// 3. 设置 BUF_NONE
layer_info.buf_mode = OT_FB_LAYER_BUF_NONE;
layer_info.mask = OT_FB_LAYER_MASK_BUF_MODE;
ioctl(fb_fd, FBIOPUT_LAYER_INFO, &layer_info);

// 4. 关闭压缩
enable = TD_FALSE;
ioctl(fb_fd, FBIOPUT_COMPRESSION_GFBG, &enable);

// 5. 设置 alpha
alpha.alpha_en = TD_TRUE;
alpha.alpha0 = alpha.global_alpha = 0xFF;
ioctl(fb_fd, FBIOPUT_ALPHA_GFBG, &alpha);

// 6. 设置 colorkey
colorkey.enable = TD_TRUE;
colorkey.value = 0x000000;
ioctl(fb_fd, FBIOPUT_COLORKEY_GFBG, &colorkey);

// 7. 打开显示
enable = TD_TRUE;
ioctl(fb_fd, FBIOPUT_SHOW_GFBG, &enable);
```

## 编译命令

```bash
/opt/linux/x86-arm/aarch64-mix210-linux/bin/aarch64-mix210-linux-gcc \
  -o vo_gfbg_init vo_gfbg_init.c \
  -I./smp/a55_linux/mpp/out/include \
  ./smp/a55_linux/mpp/out/lib/libss_mpi.a \
  ./smp/a55_linux/mpp/out/lib/libss_hdmi.a \
  ./smp/a55_linux/mpp/out/lib/libss_voice_engine.a \
  ./smp/a55_linux/mpp/out/lib/libss_upvqe.a \
  ./smp/a55_linux/mpp/out/lib/libss_dnvqe.a \
  ./smp/a55_linux/mpp/out/lib/libsecurec.a \
  -lpthread -lm -lrt -ldl
```

## 部署与运行

```bash
# 拷贝到板端
scp vo_gfbg_init root@192.168.1.168:/opt/widget_ui/

# 创建启动脚本 /opt/widget_ui/start_panel.sh
#!/bin/bash
systemctl stop weston 2>/dev/null || true
killall weston 2>/dev/null || true
sleep 1
cd /opt/widget_ui
./vo_gfbg_init
echo "面板已退出"

# 运行
chmod +x /opt/widget_ui/start_panel.sh
/opt/widget_ui/start_panel.sh
```

## 关键教训

1. **SS928 GFBG + Qt linuxfb 必须用 BUF_NONE**，不能用双缓冲。双缓冲会导致
   `yres_virtual > yres`，Qt 重新设置 vinfo 时被驱动拒绝。
2. **父进程不要 close(fb_fd)**，让子进程继承已配置好的 fb 描述符，
   避免 Qt 重新设置 vinfo 触发冲突。
3. **调试时加 `QT_QPA_FB_HIDECURSOR=1`** 和 `QT_LOGGING_RULES=qt.qpa.*=true`
   有助于快速定位问题。
4. 问题不是"完全无法渲染"，而是"渲染后立刻退出"——
   这说明 VO/HDMI/GFBG 初始化都是成功的，问题出在 Qt 接管 fb 的瞬间。
   排查这类问题时，重点看 Qt 启动后对 fb 做了哪些额外 ioctl。

---

## 第二次调试记录：项目恢复、编译链依赖、部署流程

### 背景

上一轮对话把整个项目文件改乱了，需要恢复并重新部署。

### 问题 1: vo_gfbg_init 被覆盖/丢失

**现象**: 当前工作目录下的 `vo_gfbg_init` 二进制不正确，`file` 检查发现不是 ARM64 ELF。

**解决**: 
- 回退到已知好的版本 `vo_gfbg_init_new`（由 `vo_gfbg_init_new.c` 编译而来）
- 确认该二进制: `ELF 64-bit LSB executable, ARM aarch64`，528KB，是正确的

### 问题 2: 用 SDK 静态库编译产生大量 undefined reference

**现象**: 尝试用交叉编译链 + SDK 内静态库 `.a` 重新编译 `vo_gfbg_init`:
```bash
aarch64-mix210-linux-gcc vo_gfbg_init.c \
  smp/a55_linux/mpp/out/lib/libss_mpi.a \
  smp/a55_linux/mpp/out/lib/libss_hdmi.a \
  smp/a55_linux/mpp/out/lib/libsecurec.a \
  -lpthread -lm -ldl
```
链接器报大量 undefined reference: `audio_upvqe_get_config`, `audio_dnvqe_destroy`,
`voice_encode_frame`, `voice_decode_frame`, `audio_vqe_register_module_handle` 等。

**根因**: `libss_mpi.a` 内部引用了 audio/VQE 相关库的符号（`libss_upvqe.a`,
`libss_dnvqe.a`, `libss_voice_engine.a`, `libvqe_*.a`），静态链接时
必须把所有传递依赖都列出来。

**正确编译命令**（完整依赖链）:
```bash
aarch64-mix210-linux-gcc vo_gfbg_init.c \
  -I./smp/a55_linux/mpp/out/include \
  ./smp/a55_linux/mpp/out/lib/libss_mpi.a \
  ./smp/a55_linux/mpp/out/lib/libss_hdmi.a \
  ./smp/a55_linux/mpp/out/lib/libss_voice_engine.a \
  ./smp/a55_linux/mpp/out/lib/libss_upvqe.a \
  ./smp/a55_linux/mpp/out/lib/libss_dnvqe.a \
  ./smp/a55_linux/mpp/out/lib/libsecurec.a \
  -lpthread -lm -lrt -ldl
```

**关键教训**: 
- 静态链接海思 SDK 库时，`.a` 文件的链接顺序很重要，被依赖的库要放在后面
- 如果不需要改 `vo_gfbg_init.c`，直接用预编译好的二进制即可，跳过编译步骤
- 可以根据 undefined reference 符号名反查所需库：`audio_upvqe_*` → libss_upvqe.a，
  `audio_dnvqe_*` → libss_dnvqe.a，`voice_*` → libss_voice_engine.a

### 问题 3: SSH 密码交互

**现象**: 使用 `scp` 和 `ssh` 时需要手动输入密码，阻塞自动化流程。

**解决**: 使用 `sshpass -p 'ebaina'` 透传密码：
```bash
sshpass -p 'ebaina' ssh root@192.168.1.168 "..."
sshpass -p 'ebaina' scp file root@192.168.1.168:/path/
```
板端默认密码为 `ebaina`。

### 完整部署流程

```bash
# 1. 推送 vo_gfbg_init 到板端
sshpass -p 'ebaina' scp vo_gfbg_init_new root@192.168.1.168:/opt/widget_ui/vo_gfbg_init

# 2. 推送 widget_ui 源文件
sshpass -p 'ebaina' scp 板端软件/widget_ui/main.cpp \
                            板端软件/widget_ui/Makefile \
                            root@192.168.1.168:/opt/widget_ui/

# 3. 推送辅助脚本
sshpass -p 'ebaina' scp 板端软件/run.sh \
                            板端软件/qt_env.sh \
                            板端软件/detect_touch.sh \
                            root@192.168.1.168:/opt/widget_ui/

# 4. 在板端编译 widget_ui
sshpass -p 'ebaina' ssh root@192.168.1.168 \
  "cd /opt/widget_ui && make clean && make -j\$(nproc)"

# 5. 启动面板
sshpass -p 'ebaina' ssh root@192.168.1.168 \
  "cd /opt/widget_ui && bash run.sh"
```

### 验证结果

启动后控制台输出：
```
detect_touch: 找到触摸设备 /dev/input/event0
  触摸设备: /dev/input/event0
[1] Stopping old processes...
[2] Initializing VO/HDMI/GFBG...
[vo_gfbg] === SS928 VO/HDMI panel launcher ===
[vo_gfbg] MPP system initialized OK
[vo_gfbg] VO device initialized OK
```

- HDMI 屏幕正常显示 Qt 面板
- 触摸设备 `/dev/input/event0` 自动探测并启用
- 19fps 的 RGB565 软件渲染（SS928 无 GPU 加速）
- 触摸命令经过 5 秒超时后自动重置，防止死锁

### 项目文件结构

```
工作区（PC）:
  vo_gfbg_init.c          # 源码
  vo_gfbg_init_new        # 恢复后的二进制 (ARM64, 528KB)
  板端软件/
    widget_ui/
      main.cpp            # Qt Widgets 源码
      Makefile            # 板端编译规则
    run.sh                # 启动脚本
    qt_env.sh             # Qt 环境变量
    detect_touch.sh       # 触摸设备探测
    deploy.sh             # 一键部署脚本

板端 (/opt/widget_ui/):
  vo_gfbg_init            # VO/HDMI 初始化程序
  widget_panel            # Qt 面板可执行文件
  main.cpp / Makefile     # 源文件（可在板端重新编译）
  run.sh                  # 启动脚本
  qt_env.sh               # Qt 环境变量
  detect_touch.sh         # 触摸设备探测
```

### 总结

| 步骤 | 问题 | 解决 |
|------|------|------|
| 二进制恢复 | `vo_gfbg_init` 被破坏 | 使用 `vo_gfbg_init_new` 替换 |
| 编译 | 静态库 undefined reference | 添加完整依赖链 (mpi + hdmi + voice_engine + upvqe + dnvqe) |
| 部署 | SSH 需要密码 | 使用 `sshpass -p 'ebaina'` 自动化 |
| 编译 | widget_ui 需要在板端编译 | 推源码到板端后用板载 g++ 编译 |
| 运行 | 触摸/显示 | 自动探测 event0，屏幕正常显示 |

---

## 第三次调试记录：Signal 11 (SIGSEGV) 崩溃和 GFBG 状态残留

### 问题 1: `panel killed by signal 11`

**现象**: 首次启动成功，面板正常显示约 30 秒后手动停止。第二次启动时，
`vo_gfbg_init` 初始化 GFBG 后，`widget_panel` 进程立即收到 SIGSEGV (signal 11) 崩溃。

关键日志：
```
# 首次启动（成功）:
[vo_gfbg] vinfo before: 3840x2160, bpp=16, virtual=3840x2160
[vo_gfbg] vinfo 已设为 1920x1080 ARGB1555, virtual=1920x1080
[vo_gfbg] BUF_NONE 设置成功
FBIOGET_VSCREENINFO: 1920x1080
FBIOPUT_VSCREENINFO: 1920x1080     ← Qt 写入成功，面板正常运行

# 第二次启动（崩溃）:
[vo_gfbg] vinfo before: 1920x1080, bpp=16, virtual=1920x2160  ← yres_virtual=2160 异常！
[vo_gfbg] FBIOPUT_VSCREENINFO 失败: Operation not permitted   ← 驱动拒绝
[vo_gfbg] BUF_NONE 设置成功
FBIOGET_VSCREENINFO: 1920x1080      ← Qt 读到的实际 virtual 仍是 2160
FBIOPUT_VSCREENINFO: 1920x1080      ← Qt 写入被拒绝
[vo_gfbg] panel killed by signal 11 ← SIGSEGV！
```

**根因**: 上一次运行时，GFBG 硬件层的 `yres_virtual = 2160`（可能来自更早的
BUF_DOUBLE 配置或驱动内部状态）没有被彻底清除。首次启动时因为初始 `yres_virtual = 2160`
和 `yres = 2160` 一致所以 `FBIOPUT_VSCREENINFO` 成功。但当首次运行将 `yres` 改为 1080
后，硬件仍保留了某些内部状态使 `yres_virtual` 在下次打开时变成 2160。此时：
1. `FBIOPUT_VSCREENINFO` 试图将 `yres_virtual` 从 2160 改回 1080 → 驱动拒绝（违规）
2. Qt 的 linuxfb plugin 读到的 vinfo 显示 `yres_virtual = 2160`
3. Qt 根据这个值 `mmap` 了 2160 行的缓冲区
4. 实际硬件只分配了 1080 行的帧缓冲
5. Qt 写入第 1081+ 行时 → **SIGSEGV 越界访问**

### 解决方案

**彻底重启开发板**，清除 GFBG 硬件寄存器中的所有残留状态。
重启后 `vinfo before` 恢复为干净的 `1920x1080, virtual=1920x1080`，`FBIOPUT_VSCREENINFO`
成功，面板正常运行。

同时修改了 `vo_gfbg_init.c` 中的 `vo_init()` 和 `hdmi_init()` 函数，
在初始化前先执行清理操作，以防御异常退出后的状态残留：

```c
static td_s32 vo_init(td_void) {
    // 新增：清理上一次异常退出残留的 VO 状态
    ss_mpi_vo_disable(VO_DEV);
    // ... 原有初始化代码
}

static td_s32 hdmi_init(td_void) {
    // 新增：清理上一次异常退出残留的 HDMI 状态
    ss_mpi_hdmi_stop(OT_HDMI_ID_0);
    ss_mpi_hdmi_close(OT_HDMI_ID_0);
    ss_mpi_hdmi_deinit();
    // ... 原有初始化代码
}
```

另外将 `FBIOPUT_VSCREENINFO` 失败改为软错误（不退出），因为驱动可能因内部状态
拒绝写入，但后续 Qt 可能仍能工作（如果 virtual == actual）：

```c
ret = ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var);
if (ret < 0) {
    LOG("FBIOPUT_VSCREENINFO 失败: %m（使用驱动预设值）");
    // 不退出，尝试继续
}
```

### 问题 2: `widget_panel` 二进制缺失

**现象**: 运行 `run.sh` 后报错 `execl panel failed: No such file or directory`。

**原因**: 在板端执行了 `make clean && make -j$(nproc)` 后，由于 Makefile 的依赖规则
问题，链接步骤被跳过——只生成了 `main.o` 和 `main.moc`，但没有生成 `widget_panel`。

**解决**: 再次执行 `make`（不带 clean），成功链接生成 `widget_panel`：
```
g++ -L/opt/lib -Wl,-rpath,/opt/lib -o widget_panel main.o -lQt5Widgets -lQt5Gui -lQt5Core -lpthread
```

### 本次验证结果

重启板端后完整流程正常：
```
detect_touch: 找到触摸设备 /dev/input/event0
[1] Stopping old processes...
[2] Initializing VO/HDMI/GFBG...
[vo_gfbg] MPP system initialized OK
[vo_gfbg] VO device initialized OK
[vo_gfbg] HDMI initialized OK
[vo_gfbg] vinfo before: 1920x1080, bpp=16, virtual=1920x1080        ← 干净
[vo_gfbg] vinfo 已设为 1920x1080 ARGB1555, virtual=1920x1080          ← 成功
[vo_gfbg] BUF_NONE 设置成功
[vo_gfbg] GFBG init OK: vinfo=1920x1080/16bpp, BUF_NONE, ...
[vo_gfbg] panel PID=3206 started
FBIOGET_VSCREENINFO: 1920x1080
FBIOPUT_VSCREENINFO: 1920x1080                                       ← Qt 正常
（面板持续正常运行，不再崩溃）
```

### 关键教训（第三次对话补充）

1. **GFBG 硬件状态不会随进程退出自动清理**，`yres_virtual` 可能残留为旧值。
   异常退出后最好重启板端或确保 `FBIOPUT_VSCREENINFO` 在每次启动时都成功。
2. **SIGSEGV 的根因是 mmap 大小与实际帧缓冲不匹配**：
   Qt 根据 `yres_virtual` 决定 mmap 大小，但驱动实际分配的缓冲是 `yres` 大小。
   当 `yres_virtual > yres` 时必然越界崩溃。
3. **`make clean && make` 在板端可能丢失链接步骤**，建议分开执行或检查
   Makefile 依赖关系。
4. **首次启动正常 ≠ 问题已解决**，需要连续多次启停测试才能暴露 GFBG 状态残留问题。
5. **如果 `FBIOPUT_VSCREENINFO` 连续失败且 yres_virtual 异常**，重启板端是最可靠的方案。</content>
<task_progress>
- [x] 诊断 signal 11 SIGSEGV 根因
- [x] 修改 vo_gfbg_init.c 添加清理逻辑
- [x] 重新编译部署测试
- [x] 板端重启后验证一切正常
- [x] 将第三次调试历程追加到 README
</task_progress>

