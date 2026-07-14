# 分类模型 SS928 部署说明（方案 A）

## 方案 A（当前使用）

VPSS 已把摄像头画面处理为 **224×224 NV12**，再送入 NPU。

```text
摄像头(1920x1080 NV12) -> VPSS(224x224 NV12) -> AIPP(aipp.cfg) -> OM
```

## 文件

| 文件 | 说明 |
|------|------|
| best.onnx | 分类模型 |
| aipp.cfg | 方案 A AIPP（224×224，resize=false） |
| atc_cmd.txt | 转 OM 命令 |
| label_map.txt | 5 类标签 |
| postprocess.md | softmax 后处理 |

## 模型

- 输入: `images`, `[1, 3, 224, 224]`, NCHW
- 输出: `output0`, `[1, 5]`, logits

## 转 OM

板子上进入本目录，执行 `atc_cmd.txt`，生成 `best_cls_aipp.om`

## VPSS 要求

- 输出格式: YUV420SP (NV12)
- 输出尺寸: **224 × 224**
- 缩放由 VPSS 完成，AIPP 不再 resize
