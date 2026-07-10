# 仓库内 README 索引

Agent 维护文档时：**专题细节以 `skill/` 下最新操作为准**；下列为仓库内原始 README，避免重复大段复制。

---

## 主文档（Agent 集成撰写 / 持续更新）

| 路径 | 说明 | skill 对应 |
|------|------|------------|
| [`../README.md`](../README.md) | v3.0 总文档：架构、部署、IMU、环境变量、排错 | 全专题 |
| [`operations-log.md`](operations-log.md) | 会话操作记录（比主 README 更新鲜的变更） | 优先读 |
| [`hit-replay-and-pose-rgn.md`](hit-replay-and-pose-rgn.md) | 骨骼 RGN + 击球回放 | 2026-07 |

---

## 子目录 README（引用为主）

| 路径 | 说明 |
|------|------|
| [`../models/README.md`](../models/README.md) | 分类模型替换、`import_deploy_pack_cls` |
| [`../ws73/README.md`](../ws73/README.md) | 星闪 WS73、sle_imu_bridge、部署 |
| [`../imucnn/NPU_DEPLOY.md`](../imucnn/NPU_DEPLOY.md) | IMU 1D CNN：ONNX→OM→板端 ACL |
| [`../docs/README_SS928_GFBG_Qt.md`](../docs/README_SS928_GFBG_Qt.md) | GFBG + Qt 历史调试 |
| [`../ai/README.md`](../ai/README.md) | 早期 stub 说明（attach 已实现，以 sample_vio_ai.c 为准） |
| [`../deploy_pack_cls/README.md`](../deploy_pack_cls/README.md) | PC 训练包结构 |

---

## 脚本（无独立 README，见 skill 与主 README）

| 脚本 | 用途 |
|------|------|
| `scripts/deploy.sh` | 一键部署 |
| `scripts/deploy_bin.sh` | 快速部署 AI 二进制 + run.sh |
| `scripts/deploy_panel.sh` | 增量部署 widget_panel |
| `scripts/run.sh` | 板端启动与环境变量 |
| `scripts/install_autostart.sh` | systemd 安装 |
| `scripts/build_vio_ai.sh` | 编译 sample_vio_ai |
| `scripts/build_vo_gfbg_init.sh` | 编译 vo_gfbg_init |
| `scripts/build_imu_cnn.sh` | 编译 libimu_cnn.a |
| `scripts/import_deploy_pack_imucnn.sh` | 导入 IMU CNN 模型 |

---

## 版本关系

| 目录 | 关系 |
|------|------|
| `version2.0/` | 面板 + 星闪基础，无 attach AI |
| `version3.0/` | 当前主线：分类 + 人体框 + **Pose 骨骼** + **击球回放** + IMU CNN + 对打/单人 UI |

---

## 更新约定（给 Agent）

1. **结构性变更**（新 env、新服务、新页面）：更新 `operations-log.md` + 对应专题 + 必要时 [`../README.md`](../README.md) 一节。  
2. **仅 UI 微调**：更新 `single-practice-ui.md` + `operations-log.md` 一条即可。  
3. **不要**在多个文件重复粘贴相同 env 表；以 `run.sh` 为准，skill 文档写「当前默认」并注明路径。
