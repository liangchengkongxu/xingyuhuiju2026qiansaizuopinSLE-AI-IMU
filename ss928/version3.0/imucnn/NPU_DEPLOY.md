# SS928 NPU 部署指南（羽毛球 1D CNN）

## 总体流程

```text
PyTorch (.pt)
    ↓ export_npu_onnx.py
ONNX (badminton_npu.onnx)
    ↓ AMCT (可选，INT8 量化)
量化 ONNX
    ↓ ATC
OM 离线模型 (badminton_npu.om)
    ↓ ACL C/C++ API
SS928 板端推理
```

## 1. 在 PC 上导出 ONNX

```bash
python export_npu_onnx.py
```

生成文件：

- `output/badminton_npu.onnx`：NPU 友好 ONNX（BN 已融合，固定输入 `1x8x24`）
- `deploy/badminton_preprocess.h`：CPU 端标准化参数
- `deploy/convert_to_om.sh`：ATC 转换脚本

## 2. PC 环境要求

- Ubuntu 18.04 x86_64
- SS928 SDK 中的 NNN/CANN 工具包
- 参考文档：`NNN/驱动和开发环境安装指南.pdf`

## 3. ONNX 转 OM（ATC）

### 3.1 准备环境（Ubuntu 18.04）

```bash
# 安装 SS928 SDK 里的 CANN 工具包后
source $HOME/Ascend/ascend-toolkit/latest/x86_64-linux/bin/setenv.bash
atc --help
```

把 Windows 上生成的 `output/badminton_npu.onnx` 拷到 Ubuntu 的同路径。

### 3.2 一条命令转换

在项目根目录执行：

```bash
bash deploy/convert_to_om.sh
```

或手动执行 ATC：

```bash
atc \
  --model=./output/badminton_npu.onnx \
  --framework=5 \
  --output=./output/badminton_npu \
  --input_format=ND \
  --input_shape="input:1,8,24" \
  --soc_version=OPTG \
  --output_type=FP32
```

成功后生成：`output/badminton_npu.om`

### 3.3 ATC 参数说明

| 参数 | 值 | 含义 |
|------|-----|------|
| `--model` | `badminton_npu.onnx` | 输入 ONNX |
| `--framework` | `5` | ONNX 框架 |
| `--output` | `badminton_npu` | 输出前缀（会生成 `.om`） |
| `--input_shape` | `input:1,8,24` | 固定输入 shape |
| `--input_format` | `ND` | 数据格式 |
| `--soc_version` | `OPTG` | SS928 NNN 芯片版本 |
| `--output_type` | `FP32` | 浮点模型（先跑通再用 INT8） |

### 3.4 常见报错

- `atc: command not found` → 没 source CANN 环境
- `soc_version not supported` → 查 SDK 文档确认 SS928 对应的 soc 字符串
- `Unsupported operator` → 把报错算子发出来，改 ONNX 结构
- `input shape mismatch` → 确认输入名是 `input`，shape 是 `1,8,24`

### 3.5 可选：INT8 量化（AMCT）

```bash
amct_onnx calibration \
  --model ./output/badminton_npu.onnx \
  --save_path ./output/npu_quant \
  --input_shape "input:1,8,24" \
  --data_dir ./output/npu_calib \
  --data_types "float32"
```

`npu_calib` 目录放若干 `.bin` 或 `.npy`，每个 shape 为 `(1,8,24)` 的 float32 校准样本。

## 4. 板端推理分工

| 步骤 | 位置 | 内容 |
|------|------|------|
| IMU 采集 | CPU | 10Hz 连续采样 |
| 切窗 | CPU | 以 M 峰值为中心取 24 点 |
| 标准化 | CPU | `(x - mean) / std` |
| 模型推理 | NPU | 输入 `[1,8,24]`，输出 `[1,6]` logits |
| 后处理 | CPU | softmax + argmax，映射到击球类型 |

## 5. 接入 app_main.c 的建议

```c
// 1. 环形缓冲保存最近 24 帧 8 通道数据
// 2. 检测到 M 峰值后，组装 window[8][24]
// 3. CPU 标准化
// 4. ACL 推理得到 logits[6]
// 5. 输出 class_id 和 label_name
```

## 6. 注意事项

1. **标准化必须在 CPU 做**，不要指望 NPU 自动处理原始 IMU 值。
2. **输入 shape 固定**为 `1,8,24`，不要开动态 batch。
3. 当前模型很小，NPU 主要价值是统一走 ACL 流程；若 ATC 不支持个别 1D 算子，可回退 CPU 推理。
4. 若 AMCT/ATC 报错，把报错算子名发出来，再针对性改 ONNX 结构。
