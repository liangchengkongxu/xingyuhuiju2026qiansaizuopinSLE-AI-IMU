# SS928 端 — 羽毛球智能训练面板 v3.0

> **端侧标识**：本目录为 **SS928 开发板端** 主作品代码（Qt 面板 + 摄像头 AI + NPU + 星闪 IMU 接收）。  
> 拍柄侧固件见 [`../IMU端/`](../IMU端/)（BS20 + MPU-9250）。  
> 作品总说明见 [`../README.md`](../README.md)。

海思 SS928V100 板端工程：默认使用 `camera_pipe` 预览 + `sample_vio_ai` attach 方案，同时跑 **动作分类**、**人体框选** 与 **拍柄 IMU 1D CNN 击球识别**，Qt 面板叠在 GFBG 之上。

> **开发联调参考**：会话操作记录与专题摘要见 [`skill/README.md`](skill/README.md)。

可选：`WIDGET_AI_BACKEND=modelzoo` 时使用 HiSpark 原厂 `sample_yolov8_os08a20`（与面板摄像头互斥，见文末）。

## 主要功能

| 模式 | 说明 |
|------|------|
| 单人练习 | 选技能、绑拍柄，摄像头 AI + IMU 三路 OR 触发，**回放软件画骨骼**（实时预览默认无骨骼），击球回放，教学视频与 AI 评分 |
| 对打 / 比赛 | 最多 4 台拍柄，双栏计分，IMU 触发 + 视觉更新击球类型 |
| 班级同训 | 无摄像头，1D CNN 识别 6 类击球，学员卡片实时统计 |

## 目录结构

```
version3.0/
├── README.md
├── ai/
│   └── sample_vio_ai.c          ← attach 推理（分类 + 人体框 + Region）
├── bin/
│   ├── vo_gfbg_init             ← PC 交叉编译：启动 camera_pipe + AI + Qt
│   └── sample_vio_ai            ← PC 交叉编译：NPU attach 推理
├── src/
│   ├── camera_pipe.c/h          ← VI/VPSS/VO，ch1=224 ch2=640
│   ├── vo_gfbg_init.c
│   ├── widget_yolo_action.cpp   ← Qt 读动作状态
│   └── main.cpp
├── models/
│   ├── best_aipp_fix.om         ← 分类模型（224×224，5 类 logits）
│   └── label_map.txt
├── deploy_pack_cls/             ← PC 训练包（导入后生成 models/）
├── tutorials/                   ← 单人练习教学视频
└── scripts/
    ├── build_vo_gfbg_init.sh    ← PC 编译 vo_gfbg_init
    ├── build_vio_ai.sh          ← PC 编译 sample_vio_ai
    ├── import_deploy_pack_cls.sh← 导入分类模型到 models/
    ├── deploy_models.sh         ← 同步 models/ 到板端
    ├── deploy_bin.sh          ← 快速部署 sample_vio_ai + run.sh
    ├── deploy_panel.sh        ← 增量部署 widget_panel（md5 对比）
    ├── deploy.sh                ← 一键部署（源码 + 编译 + 启动）
    ├── prepare_tutorials.sh
    ├── deploy_ws73.sh
    └── run.sh                   ← 板端启动脚本
```

---

## 架构说明（默认 attach）

### 数据流

```
VI(OS08A20) → VPSS grp0
  ├─ ch0  3840×2160  预览 + Region 人体框（实时骨骼 RGN 默认关）
  ├─ ch1   224×224   NV12 → 分类模型 best_aipp_fix.om → softmax 5 类
  ├─ ch2   640×640   attach 帧（AI 主循环 + Pose 推理）
  └─ ch3   960×540   击球回放专用（不 bind VO，worker 取帧）

vo_gfbg_init
  → camera_pipe 启 VI/VPSS/VO
  → fork sample_vio_ai attach VPSS
  → GFBG + fork widget_panel (Qt)
```

### 双模型分工

| 模型 | 板端路径 | VPSS 通道 | 输入 | 输出 | 用途 |
|------|----------|-----------|------|------|------|
| 分类 | `/opt/widget_ui/models/best_aipp_fix.om` | ch1 | 224×224 NV12 | 5 logits | 动作识别（放网/高远/平抽/杀球/挑球） |
| 人体框 | `/opt/yolov8n.om` | ch2 | 640×640 NV12 | 1×10×8400 | 画面上画人体框（10 通道 = 4 框 + 5 类，按人体几何优选） |

> **注意**：板端 `/opt/yolov8n.om` 当前为 **10 通道动作检测 OM**（非 COCO 80 类 YOLOv8n）。人体框逻辑按「大框 + 合适宽高比」选取，不依赖 COCO person 类 ID。

### 板端关键路径

| 路径 | 用途 |
|------|------|
| `/opt/widget_ui/vo_gfbg_init` | 启动器（camera + AI + Qt） |
| `/opt/widget_ui/widget_panel` | Qt 面板 |
| `/opt/widget_ui/bin/sample_vio_ai` | attach 推理 |
| `/opt/widget_ui/models/best_aipp_fix.om` | 分类模型 |
| `/opt/yolov8n.om` | 人体框检测模型 |
| `/opt/widget_ui/tutorials/*.mp4` | 教学视频 |
| `/opt/widget_ui/replays/` | 单人击球回放（PPM 帧 + meta.txt） |
| `/tmp/.widget_replay_session` | 回放 session IPC（训练中勿过早删除） |
| `/tmp/sample_vio_ai.log` | AI 日志（分类、人体框、**骨骼、回放、infer FPS**） |
| `/opt/widget_ui/models/badminton_npu.om` | 班级模式 IMU 1D CNN |
| `/tmp/widget_class_train.log` | 班级同训调试 |
| `/tmp/widget_imu.log` | 拍柄 IMU + CNN 推理 |
| `/tmp/.widget_cam_vo` | Qt → VO 小窗位置 |

---

## 部署流程（PC → 板端）

默认板端：`192.168.1.168`，用户 `root`，密码 `ebaina`，安装目录 `/opt/widget_ui`。

### 步骤 0b — 导入 IMU 1D CNN（班级模式击球类型，可选）

将 `d:\gongxiang\deploy` 导入仓库（九轴 IMU → 1D CNN → 6 类击球）：

```bash
# 共享目录已挂载时可直接：
bash scripts/import_deploy_pack_imucnn.sh

# 或指定路径：
DEPLOY_PACK_IMUCNN=/path/to/deploy bash scripts/import_deploy_pack_imucnn.sh

bash scripts/build_imu_cnn.sh   # 生成 lib/libimu_cnn.a
```

板端模型路径：`/opt/widget_ui/models/badminton_npu.om`（输入 `1×8×24`，输出 6 类 logits）。

班级同训流程：拍柄 IMU 广播 → 挥拍触发（CNN 和/或规则 FSM）→ 以 M 峰值为中心取 24 帧窗口 → NPU 推理 → 学员卡片显示「最近: 杀球/高远/…」；点击某次记录进入 **动作详情页（page 15，无视频，大字号类型）**。

```bash
# 班级模式调试日志
tail -f /tmp/widget_class_train.log
tail -f /tmp/widget_imu.log | grep -E 'hit|cnn'
```

关闭 CNN（仅统计挥拍次数）：`export WIDGET_IMU_CNN_DISABLE=1`

---

### 班级 IMU 击球检测（近期集成摘要）

#### 功能概览

| 模块 | 文件 | 作用 |
|------|------|------|
| IMU 解析 | `ws73/sle_seek_print_all/sle_seek_print_client.c` | 星闪 ADV/ScanRsp 厂商 `0xFF` 内 **22B `EB 1A 02`** → `@` 行 |
| IMU 解析 | `src/imu_swing_detector.cpp` | 解析 `@...,A*,G*,R,P,M` 行，填充 8 通道 |
| 规则挥拍 FSM | `src/imu_swing_detector.cpp` | 动态加速度 / 角速度阈值，判定「有一次挥拍」 |
| 1D CNN 分类 | `src/imu_cnn_classifier.cpp` + `imucnn/` | M 峰值窗口 → NPU → 6 类：高远/平抽/挑球/放网/发球/杀球 |
| IMU 服务 | `src/sle_imu_service.cpp` | 桥接星闪广播、合并 CNN/规则、写 `/tmp/widget_imu.log` |
| 班级 UI | `src/main.cpp` | `ClassHitDetailPage`（page 15）展示单次击球类型 |

#### 星闪广播协议（拍柄新固件 2026-07）

拍柄星闪版在 ADV **与** Scan Response 的厂商域 `0xFF` 内均携带 **22 字节二进制**：

```
EB 1A 02 DEV  uptime(2)  ax ay az  gx gy  roll pitch  mag(2)   （小端）
```

主控 `sle_seek_print_all` 解析后统一转为 `@` 行供 Qt/CNN 使用；蓝牙版仍为 ASCII Notify。

协议详情：[GitHub bs20/docs/主控对接说明.md](https://github.com/liangchengkongxu/xingyuhuiju2026qiansaizuopinSLE-AI-IMU/blob/main/bs20/docs/%E4%B8%BB%E6%8E%A7%E5%AF%B9%E6%8E%A5%E8%AF%B4%E6%98%8E.md)

#### 8 通道与训练对齐

广播行无 Gz，当前映射（与 `badminton_preprocess.h` 一致）：

| ch | 内容 | 单位 |
|----|------|------|
| 0–2 | ax, ay, az | mg |
| 3–4 | gx, gy | °/s |
| 5 | **pitch**（原误填 0，已修复） | ° |
| 6 | roll | ° |
| 7 | M 合加速度 | g |

#### `hit(cnn)` 与 `hit(rule)` 的区别

日志前缀表示 **「谁判定这次算击球」**，不是动作类型过滤；类型一律由 CNN argmax 决定。

| 日志 | 触发方式 | 分类 |
|------|----------|------|
| `hit(cnn)` | M 峰值（或软触发）+ 24 帧窗口，CNN **同时**触发并分类 | 同一次 infer |
| `hit(rule)` | 规则 FSM 认为有挥拍（dyn/gyro 阈值） | 挥拍结束后再 `classifySwing()` |

默认 `WIDGET_IMU_HIT_MODE=both`：**CNN 优先**；CNN 未命中时规则 FSM **补漏**（适合放网、挑球等慢动作）。

#### CNN 触发逻辑（`tryDetectHit`）

1. **硬触发（大力挥拍）**：M 局部峰 ≥ `WIDGET_IMU_CNN_TRIGGER_MIN_M`（默认 1.02g），dyn ≥ 0.03g，置信度 ≥ 0.24  
2. **软触发（放网/挑球）**：M ≥ 1.015g 且 dyn ≥ 0.025g，或 gyro ≥ 30°/s，置信度 ≥ 0.20  
3. 峰顶 ±1 帧各 infer 一次，取最高置信度；冷却 900ms；M 回落后 re-arm  

#### 规则 FSM 阈值（可通过环境变量调）

| 变量 | 默认 | 含义 |
|------|------|------|
| `WIDGET_IMU_RULE_SWING_ON_DYN` | 6 | 开始挥拍 dyn 阈值（×0.01g） |
| `WIDGET_IMU_RULE_MIN_PEAK_DYN` | 4 | 结束验证最小 dyn |
| `WIDGET_IMU_RULE_GYRO_ON` | 45 | 角速度辅助触发（°/s） |
| `WIDGET_IMU_RULE_MIN_PEAK_GYRO` | 35 | 结束验证最小角速度 |
| `WIDGET_IMU_RULE_CONFIRM` | 1 | 连续确认帧数 |

#### 典型日志

```bash
grep 'hit(' /tmp/widget_imu.log | tail -20
# hit(cnn) mac=... type=高远 conf=0.527 speed=13.3 M=1.04
# hit(rule) mac=... speed=29.4 type=杀球 conf=0.437 M=1.12 dyn=0.089
```

打开 6 类概率调试：`export WIDGET_IMU_CNN_DEBUG=1`（日志含 `cnn_probs` 行）。

慢动作仍漏检时可再降：

```bash
export WIDGET_IMU_CNN_SOFT_MIN_M=100      # 1.00g
export WIDGET_IMU_CNN_SOFT_MIN_DYN=1      # 0.01g
export WIDGET_IMU_RULE_SWING_ON_DYN=4
```

> 放网/挑球日志里 `speed` 偏低（十几 km/h）属正常，关键是 **能触发并分类**，不是测速仪精度。

---

## 骨骼 Pose 与单人击球回放（2026-07）

### 骨骼（实时 vs 回放）

- 模型：`/opt/widget_ui/models/best_pose_aipp.om`（VPSS ch1/ch2 640×640 推理）
- **实时预览**：默认 `WIDGET_POSE_RGN=0`，HDMI/面板**不画**硬件骨骼（减轻 RGN 与 ch0 抢资源）
- **击球回放**：`pose_stamp_on_replay_nv12()` 软件画骨架到导出 PPM；BT.601 limited-range 转 RGB
- 若需恢复实时骨骼：设 `WIDGET_POSE_RGN=1` 后重启

```bash
tail -f /tmp/sample_vio_ai.log | grep -E 'infer fps|ai_pose|hit_replay'
cat /proc/umap/npudev
```

### 击球回放

- 训练页每次击球 → `sample_vio_ai` 从 **VPSS ch3** 采集前后各 2s → `/opt/widget_ui/replays/{session}/hit_{N}/`
- 总结页 / 动作详情页播放；板端无 ffmpeg 时用 **PPM 帧序列**（`FrameReplayWidget`）
- 输出 **960×540 @ 20fps**；前 3 次击球立即导出带骨骼 PPM，第 4 次起仅存 raw，**点进详情页再渲染骨骼**

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_POSE_RGN` | `0` | 实时 RGN 骨骼（0=关，仅回放 stamp） |
| `WIDGET_REPLAY_LIVE` | `1` | 击球前环形缓冲 |
| `WIDGET_REPLAY_FPS` | `20` | 回放采集/导出帧率 |
| `WIDGET_REPLAY_VPSS_CHN` | `3` | 回放专用通道（**勿用 0**，会抢 ch0 预览） |
| `WIDGET_REPLAY_POSE_EAGER_MAX` | `3` | 立即渲染骨骼的击球序号上限 |
| `WIDGET_REPLAY_SRC_NV21` | `1` | ch3 回放 UV 顺序 |

Agent 详细说明：[`skill/hit-replay-and-pose-rgn.md`](skill/hit-replay-and-pose-rgn.md)

---

### 步骤 0 — 准备分类模型（首次或换模型）

将 PC 上的 `deploy_pack_cls` 拷入仓库，执行导入：

```bash
# 示例：把训练包放到 version3.0/deploy_pack_cls/
cp -r /path/to/deploy_pack_cls/* version3.0/deploy_pack_cls/

cd version3.0
bash scripts/import_deploy_pack_cls.sh
```

脚本会：

- 将 `best_cls_aipp.om`（或包内优先 om）安装为 `models/best_aipp_fix.om`
- 同步 `label_map.txt`、`aipp.cfg`、`data.yaml`
- 若类别数 `nc` 变化，自动更新 `ai/sample_vio_ai.c` 中 `YOLO_NUM_CLASSES`

人体框模型需单独放到板端（若尚未部署）：

```bash
sshpass -p ebaina scp -o StrictHostKeyChecking=no /path/to/yolov8n.om \
  root@192.168.1.168:/opt/yolov8n.om
```

### 步骤 1 — PC 交叉编译 AI 组件

需安装海思交叉编译器 `aarch64-mix210-linux-gcc`（SDK 自带路径 `/opt/linux/x86-arm/aarch64-mix210-linux/bin`）。

```bash
cd /path/to/SS928V100_SDK_V2.0.2.2/version3.0

# 编译 attach 推理（分类 + 人体框）
bash scripts/build_vio_ai.sh
ls -lh bin/sample_vio_ai

# 编译 camera_pipe 启动器
bash scripts/build_vo_gfbg_init.sh
ls -lh bin/vo_gfbg_init
```

> 若 `import_deploy_pack_cls.sh` 修改了 `YOLO_NUM_CLASSES`，必须先 `build_vio_ai.sh` 再部署。

### 步骤 2 — 部署到板端

```bash
cd version3.0

# 一键：传源码 + bin + 模型 + 板端编译 Qt + 启动
bash scripts/deploy.sh

# 或分步：
bash scripts/deploy_models.sh   # 仅同步 models/ → /opt/widget_ui/models/
bash scripts/deploy.sh          # WIDGET_DEPLOY_SKIP_MODELS=1 可跳过模型
```

`deploy.sh` 会：

1. 停止旧进程，上传 `src/`、`bin/vo_gfbg_init`、`bin/sample_vio_ai`、`run.sh` 等
2. 可选同步 `models/`、`tutorials/`、星闪工具；配置 AICPU 软链
3. 在板端 `make` 编译 `widget_panel`（校验非空且为 aarch64）
4. 执行 `run.sh` 启动（设 `WIDGET_DEPLOY_SKIP_RUN=1` 可只部署不启动）

> **编译与启动不要并行**：等 `make` 结束后再 `bash run.sh`，否则可能得到 0 字节 `widget_panel`（见排错 §9）。

仅更新 AI 二进制（不重新编译面板）：

```bash
sshpass -p ebaina scp -o StrictHostKeyChecking=no \
  bin/sample_vio_ai root@192.168.1.168:/opt/widget_ui/bin/

ssh root@192.168.1.168 "killall sample_vio_ai vo_gfbg_init widget_panel 2>/dev/null; sleep 2; cd /opt/widget_ui && bash run.sh"
```

### 步骤 3 — 板端启动与验证

```bash
ssh root@192.168.1.168
cd /opt/widget_ui
bash run.sh
```

进入 **单人练习 → 摄像头/动作页** 后检查：

```bash
# 1. 进程
ps | grep -E 'sample_vio_ai|vo_gfbg_init|widget_panel'

# 2. 分类日志（约每 30 帧一行）
grep cls /tmp/sample_vio_ai.log | tail -10
# 期望: cls: frame=31 cls=2(pingchou) score=0.856 stable=1

# 3. 人体框日志（约每 60 帧一行）
grep person_draw /tmp/sample_vio_ai.log | tail -5
# 期望: person_draw: det=20 pick=1 cls=... score=0.xx box=(...) feat=10 det=640x640 preview=3840x2160

# 4. 实时跟踪
tail -f /tmp/sample_vio_ai.log | grep -E 'cls:|person_draw'

# 5. Qt 当前动作状态
cat /tmp/.widget_yolo_action

# 6. 模型加载信息
grep -E 'ai_init|ai_det' /tmp/sample_vio_ai.log | head -5
```

正常时 HDMI/小窗应显示 **人体绿框**，界面动作标签随 `cls:` 输出变化。

### 步骤 4 — 教学视频（可选）

```bash
# PC 转码（H.264 无音轨）
bash scripts/prepare_tutorials.sh

# 随 deploy.sh 上传，或单独 scp
scp tutorials/*.mp4 root@192.168.1.168:/opt/widget_ui/tutorials/
```

---

## 环境变量（run.sh 默认）

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_AI_BACKEND` | `attach` | `attach`=camera_pipe+sample_vio_ai；`modelzoo`=原厂样例 |
| `WIDGET_AI_MODE` | `cls` | `cls`=分类模型；未设置或非 cls 时按 YOLO 检测处理 |
| `WIDGET_AI_MODEL` | `/opt/widget_ui/models/best_aipp_fix.om` | 分类 om 路径 |
| `WIDGET_YOLO_DET_MODEL` | `/opt/yolov8n.om` | 人体框检测 om |
| `WIDGET_YOLO_DET_CHN` | `2` | 检测用 VPSS 通道 |
| `WIDGET_YOLO_PERSON_CONF` | `0.25` | 人体框置信度 |
| `WIDGET_YOLO_PERSON_SMOOTH` | `0.55` | 人体框平滑系数 |
| `WIDGET_YOLO_CONF_PERCENT` | `50` | 动作显示阈值（%） |
| `WIDGET_CAM_MIPI` | `1` | MIPI 插座（0/1） |
| `WIDGET_IMU_CNN_MODEL` | `/opt/widget_ui/models/badminton_npu.om` | 班级模式 1D CNN 模型 |
| `WIDGET_IMU_CNN_DISABLE` | 未设置 | `1` 关闭 IMU 击球类型识别 |
| `WIDGET_IMU_HIT_MODE` | `both` | `cnn` / `rule` / `both`（CNN 优先 + 规则补漏） |
| `WIDGET_IMU_CNN_TRIGGER_CONF` | `0.24` | CNN 硬触发最低置信度 |
| `WIDGET_IMU_CNN_COOLDOWN_MS` | `900` | 同 MAC 两次击球最小间隔（ms） |
| `WIDGET_IMU_CNN_TRIGGER_MIN_M` | `102` | 硬触发 M 峰值（×0.01g，即 1.02g） |
| `WIDGET_IMU_CNN_TRIGGER_MIN_DYN` | `3` | 硬触发 dyn 阈值（×0.01g） |
| `WIDGET_IMU_CNN_SOFT_MIN_M` | `101` | 软触发 M 阈值（放网/挑球） |
| `WIDGET_IMU_CNN_SOFT_MIN_DYN` | `2` | 软触发 dyn 阈值 |
| `WIDGET_IMU_CNN_SOFT_MIN_GYRO` | `30` | 软触发角速度（°/s） |
| `WIDGET_IMU_CNN_SOFT_CONF` | `0.20` | 软触发最低置信度 |
| `WIDGET_IMU_CNN_CLASS_CONF` | `0.22` | 规则路径 `classifySwing` 最低置信度 |
| `WIDGET_IMU_CNN_DEBUG` | 未设置 | `1` 时在 log 打印 6 类 softmax |
| `WIDGET_IMU_RULE_*` | 见上文 IMU 节 | 规则 FSM 阈值 |
| `WIDGET_WS73_SKIP_WIFI` | `1` | 跳过 `wifi_sta.sh` 避免刷屏 |
| `WIDGET_WS73_SKIP_LEGACY_CLIENT` | `1` | 跳过旧 `sle_client.sh` |
| `WIDGET_POSE_RGN` | `0` | 实时 RGN 骨骼（0=关） |
| `WIDGET_POSE_CONF` | `0.10` | Pose 检测置信度 |
| `WIDGET_POSE_KPT_VIS` | `0.25` | 关键点显示阈值 |
| `WIDGET_POSE_INTERVAL` | `2` | 骨骼每 N 帧推理一次 |
| `WIDGET_REPLAY_LIVE` | `1` | 击球回放 live 环形缓冲 |
| `WIDGET_REPLAY_FPS` | `20` | 回放采集帧率 |
| `WIDGET_REPLAY_VPSS_CHN` | `3` | 回放 VPSS 通道（**勿用 0**） |
| `WIDGET_REPLAY_POSE_EAGER_MAX` | `3` | 立即导出骨骼的回放条数 |
| `ASCEND_AICPU_KERNEL_PATH` | `/opt/lib/npu` | ACL AICPU 算子（IMU CNN 必需） |
| `LD_LIBRARY_PATH` | — | 需含 `/opt/lib/npu`（run.sh 已设置） |

关闭 AI、仅摄像头预览：

```bash
export WIDGET_AI_DISABLE=1
cd /opt/widget_ui && bash run.sh
```

---

## 常用板端命令

```bash
# 启动面板 + 摄像头 + AI
cd /opt/widget_ui && bash run.sh

# 分类日志（实时）
tail -f /tmp/sample_vio_ai.log | grep cls

# 人体框日志
grep person_draw /tmp/sample_vio_ai.log | tail -10

# IMU 广播（班级同训）
grep 'SLE_IMU' /tmp/sle_imu_lines | head
tail -f /tmp/widget_class_train.log

# 确认模型文件
ls -lh /opt/widget_ui/models/ /opt/yolov8n.om

# 确认教学视频
ls -lh /opt/widget_ui/tutorials/
```

---

## 联调记录与排错

### 1. 班级同训：多拍柄各自计数

**现象**：扫描到 2 台拍柄，只挥动其中 1 根，两台学员挥拍次数同步上涨。

**处理**：IMU 行带 MAC 解析；每个 MAC 独立挥拍检测；`ClassTrainPage` 按 MAC 给对应学员 +1。

**验证**：只挥动一根拍柄时，仅对应学员卡片计数增加。

### 2. 单人练习动作列表（5 项）

杀球、放网、高远、平抽、挑球。代码：`PracticePage` / `src/main.cpp`。

### 3. 教学视频无法播放（AAC / HEVC）

板端 GStreamer 无 AAC 解码器。PC 执行 `prepare_tutorials.sh` 转 H.264 + 去音轨后再部署。

### 4. 人体框不跟人 / 偏到画面右侧

**原因**：

- 误把 10 通道动作 OM 当 COCO 80 类解析，或坐标重复缩放。
- Region 坐标系与 VPSS ch0 预览（3840×2160）不一致。

**处理**（已在 `sample_vio_ai.c` 修复）：

- 10 通道模型：5 类 decode + `person` 几何优选大框。
- 框保持在 640×640 检测空间，由 Region **一次性** 映射到 ch0 预览。

**排查**：

```bash
grep -E 'ai_det output|person_draw|show_n=' /tmp/sample_vio_ai.log | tail -20
```

期望 `pick=1`、`show_n=1`，框坐标随人移动。

### 5. 分类无输出

- 确认已进入摄像头页面（`sample_vio_ai` 已启动）。
- 确认 `WIDGET_AI_MODE=cls` 且 `best_aipp_fix.om` 存在。
- 查看 `grep cls /tmp/sample_vio_ai.log`。

### 6. deploy 后 MPP 初始化失败

若 `killall` 后 `camera_pipe_mpp_prepare failed`，重启板子或等几秒后再 `bash run.sh`。

### 7. IMU CNN 只显示「平抽」/ 漏检慢动作

**原因**：

- 早期 **ch5 误填 0**（应为 pitch），输入与训练不一致，模型易偏向某一类。
- 触发门槛按大力挥拍设定，**放网、挑球** M 峰值仅约 1.01–1.03g，原 CNN/规则均难触发。

**处理**：

- 已修复 ch5=pitch；CNN 增加软触发路径；规则 FSM 降阈值；默认 `WIDGET_IMU_HIT_MODE=both`。
- 用 `grep hit /tmp/widget_imu.log` 区分 `hit(cnn)` / `hit(rule)`；必要时 `WIDGET_IMU_CNN_DEBUG=1` 看 6 类概率。

### 8. IMU CNN / ACL：`libmsprofiler.so` 或 AICPU 报错

**处理**：`run.sh` 已设 `ASCEND_AICPU_KERNEL_PATH=/opt/lib/npu`，`deploy.sh` 会在板端建立 `/usr/lib64/aicpu_kernels` → `/opt/lib/npu` 软链。确认：

```bash
ls -l /usr/lib64/aicpu_kernels/libcpu_kernels.so
echo $ASCEND_AICPU_KERNEL_PATH
```

### 9. `run.sh` 报 `execl panel failed: Exec format error`

**原因**：`/opt/widget_ui/widget_panel` 为 **0 字节空文件**（编译未完成时启动 `run.sh`，`killall` 与链接竞态导致）。

**处理**：

```bash
cd /opt/widget_ui && make
file widget_panel   # 应显示 ARM aarch64，且非 empty
bash run.sh
```

`run.sh` 已在启动前检查：若 `widget_panel` 为空或非 aarch64 会直接报错并提示先 `make`。

**注意**：不要与 `deploy.sh` 并行执行；改代码后 **先 `make` 完成再 `run.sh`**。

### 10. 星闪日志刷屏 `wifi_sta_stop` / `sle_client_stop`

**处理**：默认 `WIDGET_WS73_SKIP_WIFI=1`、`WIDGET_WS73_SKIP_LEGACY_CLIENT=1`；IMU 走 `sle_imu_bridge.sh` 轻量 prep。

### 11. 骨骼不显示 / 回放后系统异常

**原因**：从 **VPSS ch0** 高频取帧做回放，与 VO 预览抢同一通道，导致预览饿死、`sample_vio_ai` 反复崩溃、`vb_set_conf failed`。

**处理**：

- `WIDGET_REPLAY_VPSS_CHN=3`（专用 960×540 回放通道，勿用 0/ch0）
- 实时骨骼默认关（`WIDGET_POSE_RGN=0`），回放由软件 stamp
- 板端 **reboot** 清 MPP 后再 `bash run.sh`
- 详见 [`skill/hit-replay-and-pose-rgn.md`](skill/hit-replay-and-pose-rgn.md)

### 12. 回放一直「正在生成」

**原因 A**：离开训练页时过早 `clearReplaySession()`，post 采集断帧。  
**处理**：已改为仅在 `goHome()` 清除 session。

**原因 B**：第 4 次及以后回放仅保存 raw，需点进详情页触发骨骼渲染（显示「骨骼渲染中」）。  
**处理**：等待数秒或调大 `WIDGET_REPLAY_POSE_EAGER_MAX` 预渲染更多条。

---

## 可选：modelzoo 模式（原厂 YOLOv8 样例）

与 attach **互斥**（独占 VI/VPSS/VO，不能与 Qt 摄像头页同时使用）。

```bash
export WIDGET_AI_BACKEND=modelzoo
cd /opt/widget_ui && bash run.sh
```

| 路径 | 用途 |
|------|------|
| `/opt/sample/yolov8/sample_yolov8_os08a20` | 原厂推理 |
| `/opt/sample/yolov8/yolov8n.om` | deploy_models.sh 从 models 复制 |
| `/tmp/sample_yolov8.log` | 日志 |

单独测 modelzoo（不启 Qt）：

```bash
sh /opt/widget_ui/start_yolov8_modelzoo.sh
tail -f /tmp/sample_yolov8.log
```

---

## 参考

- **Agent 技能包（操作记录 + 专题）**：[`skill/README.md`](skill/README.md)
- 分类模型说明：`deploy_pack_cls/README.md`
- 模型目录说明：`models/README.md`
- 历史 attach 脚本：`deploy_pack_v2/deploy_pack_v2/start_ai_camera.sh`
- modelzoo：[HiSpark yolov8s README](https://gitee.com/HiSpark/modelzoo/blob/master/samples/samples_GPL/built-in/yolov8s/README.md)
- version2.0：`../version2.0/README.md`
