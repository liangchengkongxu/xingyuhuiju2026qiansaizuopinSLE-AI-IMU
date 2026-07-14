# YOLOv8 后处理与坐标反变换说明

适用场景：`SS928 + 摄像头(1920x1080) + YOLOv8(640x640)`。

## 1) 推荐前处理策略

- 训练侧通常是 letterbox 到 `640x640`。
- 部署侧建议保持一致：先按比例缩放，再补边（padding 值建议 `114`）。
- 设原图尺寸为 `(W0,H0)`，网络输入为 `(640,640)`：
  - `r = min(640/W0, 640/H0)`
  - `new_w = round(W0 * r)`
  - `new_h = round(H0 * r)`
  - `pad_w = (640 - new_w) / 2`
  - `pad_h = (640 - new_h) / 2`

## 2) 检测框反变换（映射回原图）

若网络输出框为 letterbox 坐标系下的 `xyxy`（单位：像素）：

- `x1 = (x1_l - pad_w) / r`
- `y1 = (y1_l - pad_h) / r`
- `x2 = (x2_l - pad_w) / r`
- `y2 = (y2_l - pad_h) / r`

然后做边界裁剪：

- `x1,x2` 裁剪到 `[0, W0-1]`
- `y1,y2` 裁剪到 `[0, H0-1]`

## 3) NMS 推荐参数（起步值）

- `conf_thres = 0.25`
- `iou_thres = 0.45`
- `max_det = 300`

说明：如果误检偏多，先提高 `conf_thres`；如果漏检偏多，先降低 `conf_thres` 或适度提高 `max_det`。

## 4) 与当前打包文件的对应关系

- 模型输入：`best.onnx` -> `images:1,3,640,640`（NCHW）
- AIPP：`aipp.cfg`（当前为 `YUV420SP_U8` + `csc_switch=true` + `1/255`）
- 标签：`label_map.txt`

## 5) 常见问题排查

- 框整体偏移：通常是 `pad_w/pad_h` 或 `r` 使用不一致。
- 框尺度异常：通常是把直接 resize 当成 letterbox，或反变换公式没扣 padding。
- 颜色异常导致精度下降：检查输入格式是否真的是 `YUV420SP(NV12)`，以及 CSC 是否开启。
