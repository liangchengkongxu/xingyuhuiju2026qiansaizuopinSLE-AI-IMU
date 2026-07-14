# IMU 1D CNN 与击球检测

主文档：[`../README.md`](../README.md)「班级 IMU 击球检测」。  
代码：`imu_swing_detector.cpp`、`imu_cnn_classifier.cpp`、`sle_imu_service.cpp`。

---

## 8 通道映射（广播 → CNN）

| ch | 内容 | 单位 |
|----|------|------|
| 0–2 | ax, ay, az | mg |
| 3–4 | gx, gy | °/s |
| 5 | **pitch** | ° |
| 6 | roll | ° |
| 7 | M | g |

广播行格式：`@t,Ax,Ay,Az,Gx,Gy,R,P,M`（`sle_imu_service` 解析）。

---

## hit(cnn) vs hit(rule)

| 日志 | 含义 |
|------|------|
| `hit(cnn)` | M 峰/软触发 + 24 帧窗口，CNN 同时触发并分类 |
| `hit(rule)` | 规则 FSM 判定挥拍结束，再 `classifySwing()` |

默认 `WIDGET_IMU_HIT_MODE=both`：先 CNN，未中则规则。

---

## CNN 触发（tryDetectHit）

1. **硬触发**：M ≥ 1.02g，dyn ≥ 0.03g，conf ≥ `WIDGET_IMU_CNN_TRIGGER_CONF`（现 **0.26**）
2. **软触发**（放网/挑球）：M/dyn/gyro 较低门槛，conf ≥ `WIDGET_IMU_CNN_SOFT_CONF`（0.22）
3. 峰顶 ±1 帧 infer 取最高 conf；冷却 900ms

---

## 规则 FSM + 大幅波动（burst）

`imu_swing_detector.cpp` 中 `isLargeBurst()`：

- `peakDyn ≥ WIDGET_IMU_BURST_MIN_DYN`（默认 8 → 0.08g）**或**
- `peakGyro ≥ WIDGET_IMU_BURST_MIN_GYRO`（默认 58°/s）**或**
- 两者同时达 72% 阈值

满足 burst 时降低 finish 门槛，便于「大幅波动」单独触发。

### 规则阈值（run.sh 当前默认）

| 变量 | 默认 | 含义 |
|------|------|------|
| `WIDGET_IMU_RULE_SWING_ON_DYN` | 7 | 开始挥拍（×0.01g） |
| `WIDGET_IMU_RULE_MIN_PEAK_DYN` | 5 | 结束最小 dyn |
| `WIDGET_IMU_RULE_GYRO_ON` | 50 | 角速度辅助 |
| `WIDGET_IMU_BURST_MIN_DYN` | 8 | 大幅波动 dyn |
| `WIDGET_IMU_BURST_MIN_GYRO` | 58 | 大幅波动 gyro |

---

## 单人练习：三路 OR + 去重

`TrainingPage`（非班级模式）：

1. **摄像头稳定识别** ≥ 52% → 计数  
2. **摄像头挥拍事件** ≥ 48%（`WIDGET_YOLO_SWING=1`）  
3. **拍柄 IMU**（CNN 或 burst/rule）→ 计数  

`tryAcceptPracticeHit()`：`WIDGET_PRACTICE_HIT_COOLDOWN_MS=950` 内不重复计。

`WIDGET_HIT_SOURCE=both`；击球**类型 UI** 仍固定为 `m_currentSkill`。

---

## 调试

```bash
tail -f /tmp/widget_imu.log | grep hit
export WIDGET_IMU_CNN_DEBUG=1   # 6 类概率
tail -f /tmp/sle_imu_lines
```

慢动作漏检：降 `WIDGET_IMU_CNN_SOFT_MIN_M`、`WIDGET_IMU_RULE_SWING_ON_DYN`（见主 README）。

---

## 模型与编译

```bash
bash scripts/import_deploy_pack_imucnn.sh
bash scripts/build_imu_cnn.sh
# 板端：/opt/widget_ui/models/badminton_npu.om
```

NPU 导出流程：[`../imucnn/NPU_DEPLOY.md`](../imucnn/NPU_DEPLOY.md)
