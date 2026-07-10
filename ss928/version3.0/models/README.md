# AI 模型目录

板端推理仍使用固定路径 **`/opt/widget_ui/models/best_aipp_fix.om`**（`vo_gfbg_init` / `sample_vio_ai attach` 不变），仅替换模型文件与标签。

## 更换为新训练模型（deploy_pack_cls）

1. 将 PC 上的 `deploy_pack_cls` 拷到本仓库：

```bash
# 示例：WSL
cp -r /mnt/c/Users/hp/Desktop/deploy_pack_cls/* version3.0/deploy_pack_cls/
```

2. 导入到 `models/`：

```bash
cd version3.0
bash scripts/import_deploy_pack_cls.sh
```

3. 若 `data.yaml` 里 `nc` 与旧模型不同，重编 AI 并部署：

```bash
bash scripts/build_vio_ai.sh
bash scripts/deploy_models.sh
bash scripts/deploy.sh
```

`import_deploy_pack_cls.sh` 会：
- 将包内 `.om` 安装为 `best_aipp_fix.om`
- 同步 `label_map.txt` / `aipp.cfg` / `data.yaml`
- 按 `nc` 自动更新 `ai/sample_vio_ai.c` 中的 `YOLO_NUM_CLASSES`

Qt 面板从 `models/label_map.txt` 读取类别中文名（`widget_yolo_action.cpp`），无需改 UI 代码即可适配新标签。

## 当前默认六类（旧模型）

| ID | 英文名 | 界面中文 |
|----|--------|----------|
| 0 | Clear shot | 高远 |
| 1 | Drive shot | 平抽 |
| 2 | Drop-shot | 吊球 |
| 3 | Lift Shot | 挑球 |
| 4 | Serve | 发球 |
| 5 | Smash shot | 杀球 |

## 板端路径

| 路径 | 用途 |
|------|------|
| `/opt/widget_ui/models/best_aipp_fix.om` | attach 推理加载 |
| `/opt/sample/yolov8/yolov8n.om` | modelzoo 模式（deploy 时从 models 复制） |
| `/tmp/.widget_yolo_action` | AI → Qt 动作状态 |
