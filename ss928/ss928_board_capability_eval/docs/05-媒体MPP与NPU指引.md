# 05 — 媒体 MPP 与 NPU 测评指引

本包不内嵌完整海思 SDK。测评媒体/NPU 时，请在 PC 上打开 **SS928 SDK**（常见结构含 `smp/a55_linux/mpp/`）。

## MPP 通路概念

典型摄像头预览链：

```
Sensor → MIPI → VI → VPSS → VO (HDMI)
```

多通道 VPSS 可把「预览 / AI 输入 / 回放」拆开，避免抢同一通道导致预览饿死或 VB 耗尽。

## 建议先跑的原厂 sample

路径示例（以 SDK 为准）：

| sample | 测什么 |
|--------|--------|
| `smp/a55_linux/mpp/sample/vio` | VI→VPSS→VO 预览 |
| `.../sample/gfbg` | 图形帧缓冲 |
| `.../sample/hdmi` | HDMI |
| `.../sample/region` | Region 画框叠加 |
| `.../sample/svp` / `svp_npu` | 智能视觉 / NPU |

编译一般在 SDK 对应 `sample` 目录 `make`，产物拷到板端运行。MIPI 插座号、sensor 型号因硬件而异，需对照板厂说明（常见环境变量或命令行 `--mipi=`）。

## NPU / ACL

板上常见库目录：`/opt/lib/npu`（以实际镜像为准）。

测评点：

1. 库是否存在、`LD_LIBRARY_PATH` 是否包含
2. 原厂 sample 能否 `LoadModel` + 推理
3. 自定义模型：PC 侧 ATC → `.om` → 板端加载

异常后若出现 `vb_set_conf failed` / `mpi` 初始化失败：先 kill 相关进程，不行则 **reboot** 再测。

## 资源注意

- 大分辨率多通道会吃 VB；sample 失败时先查 VB 配置
- 长时间压测注意根分区空间（日志、落盘帧）
- 进程异常退出后 MPP 状态可能残留，reboot 是最稳妥的清场手段
