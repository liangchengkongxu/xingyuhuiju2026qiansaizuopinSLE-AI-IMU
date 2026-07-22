# 海思 SS928 开发板 — 能力测评包（通用）

面向 **SS928V100 开发板** 的离线能力摸底与远程开发，**与具体业务应用无关**。

适用场景：换板验证、队友交接、答辩前硬件能力自检、星闪/显示/媒体/NPU 基线测试。

---

## 你手里有什么

```
ss928_board_capability_eval/
├── README.md                      ← 本文件（入口）
├── board.env.example              ← 板子 IP/账号模板
├── docs/
│   ├── 01-能力清单与测评步骤.md   ← 建议按这个清单测
│   ├── 02-SSH与远程部署.md
│   ├── 03-星闪WS73测评.md
│   ├── 04-显示GFBG与Qt要点.md
│   └── 05-媒体MPP与NPU指引.md
├── scripts/
│   ├── board_common.sh            ← SSH/SCP 公共环境
│   ├── check_board.sh             ← 连通性 + 基础环境
│   ├── ssh_board.sh               ← 一键登录 / 远程命令
│   └── probe_board_capability.sh  ← 一键采集板端能力快照
└── sle_throughput_test/           ← 星闪链路吞吐测速（可选）
```

---

## 5 分钟上手

```bash
# 1. PC 依赖
sudo apt install sshpass rsync

# 2. 配置板子 IP
cd ss928_board_capability_eval
cp board.env.example board.env
# 编辑 board.env：改 BOARD_IP（账号密码同厂商板通常不变）

# 3. 连通性
bash scripts/check_board.sh

# 4. 能力快照（输出到 /tmp/ss928_probe_*.txt，并 scp 回本机）
bash scripts/probe_board_capability.sh

# 5. 登录板子
bash scripts/ssh_board.sh
```

默认常见账号：`root` / `ebaina`，IP：`192.168.1.168`。

---

## 测评建议顺序

| 顺序 | 模块 | 文档 / 脚本 |
|------|------|-------------|
| 1 | 网络 / SSH / 存储 / CPU | `check_board.sh` + `probe_board_capability.sh` |
| 2 | 显示 / framebuffer | docs/04 |
| 3 | 摄像头 / MPP sample | docs/05（需完整 SDK） |
| 4 | NPU / ACL | docs/05 |
| 5 | 星闪 WS73 吞吐 | docs/03 + `sle_throughput_test/` |

详细打勾清单见：**[docs/01-能力清单与测评步骤.md](docs/01-能力清单与测评步骤.md)**。

---

## 与完整 SDK 的关系

本包 **不包含** 海思完整 BSP / MPP 源码树（体积大）。  
若要测摄像头预览、编码、原厂 SVP/NPU sample，请在有 **SS928 SDK** 的 PC 上，按 docs/05 进入：

`smp/a55_linux/mpp/sample/`

星闪编译链接库 `libsle_host.a` 需原厂 **WS73 SDK** 解压目录（见 docs/03）。

---

## 打包给队友

整个目录可直接 zip 发送：

```bash
cd ..
zip -r ss928_board_capability_eval.zip ss928_board_capability_eval \
  -x '*/board.env' -x '*/*.o' -x '*/.git/*'
```

不要把含真实密码的 `board.env` 打进去（只发 `board.env.example`）。
