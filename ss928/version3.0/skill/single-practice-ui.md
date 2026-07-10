# 单人练习 UI 与页面流

代码：`version3.0/src/main.cpp`。

---

## 页面流

```
Home → SinglePage（单人/对打）
  → SinglePracticeSetupPage（扫拍柄，page 14）
  → PracticePage（5 动作，page 2）
  → SkillDetailPage（视频 + 要领 + 开始练习，page 3）
  → TrainingPage（训练中，page 4）
  → TrainingSummaryPage
```

绑定拍柄：`beginSinglePracticeAfterBind()` → 开 IMU → 进 PracticePage。

---

## PracticePage 动作列表

杀球、放网、高远、平抽、挑球（5 项）。

---

## SkillDetailPage 布局（当前）

```
┌────────────────┬─────────────────────────┐
│ 动作要领 448px  │  教学视频 1280×720       │
│ 标题 36px       │                         │
│ 要点/注意       │  [开始练习] 1160×132px  │
│ 条目 30px       │  绿色按钮，略窄于视频    │
│ stretch 均分竖向 │  margin-top 40px        │
└────────────────┴─────────────────────────┘
```

- 已移除左侧 Sidebar
- 右上角：**返回上级=绿**，**返回首页=红**（仅本页 inline stylesheet）

### 常量（main.cpp）

```cpp
kSkillDetailLeftColW = 448
kSkillDetailVideoW   = 1280
kSkillDetailVideoH   = 720   // 16:9
kSkillDetailStartBtnW = 1160
```

### 要领文案

- `skillTipsPoints()` / `skillTipsWarnings()`：五动作各 2 条要点 + 2 条注意
- `refreshTipsContent()`：动态重建左侧 layout + stretch

改文案只改上述两个函数，勿加长段落（字号已 30px，需控制行数）。

### 教学视频

- 路径：`/opt/widget_ui/tutorials/<技能名>.mp4`
- PC 转码：`bash scripts/prepare_tutorials.sh`（H.264，无音轨）

---

## TrainingPage 单人模式

- 击球类型：`refreshFixedSkillTypeLabel()` → 始终显示 `m_currentSkill`
- 击球计数：见 [`imu-cnn-and-hit-detection.md`](imu-cnn-and-hit-detection.md)
- 提示文案：`击球：摄像头高置信 / 拍柄大幅波动 / CNN 九轴…`

班级学员模式（`startClassStudentTraining`）仍用 CNN/YOLO 识别类型，布局不同。

---

## SinglePracticeSetupPage

- 「下一步」**始终显示**；扫到设备即可点
- 点下一步会 `stopScan()` 提前结束

---

## 改 UI 后的部署

```bash
scp version3.0/src/main.cpp root@192.168.1.168:/opt/widget_ui/
ssh root@192.168.1.168 "cd /opt/widget_ui && rm -f main.o main.moc && make"
systemctl restart widget_ui.service   # 或 reboot 若 MPP 卡住
```
