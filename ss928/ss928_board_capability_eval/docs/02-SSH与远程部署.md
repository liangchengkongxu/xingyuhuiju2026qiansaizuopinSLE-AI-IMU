# 02 — SSH 与远程部署

## 配置

```bash
cd ss928_board_capability_eval
cp board.env.example board.env
# 编辑：
#   export BOARD_IP="192.168.1.xxx"
#   export BOARD_USER="root"
#   export BOARD_PASS="ebaina"
```

## 常用命令

```bash
bash scripts/check_board.sh          # ping + SSH + 板端基础信息
bash scripts/ssh_board.sh            # 交互登录
bash scripts/ssh_board.sh "uname -a" # 远程单条命令
bash scripts/probe_board_capability.sh
```

## 远程部署通用模式（板上编译）

很多板端程序采用：

1. PC：`scp` 源码到 `/tmp/xxx`
2. 板端：`gcc` / `make`（aarch64）
3. 安装到 `/opt/...`

```bash
source scripts/board_common.sh   # 会读 board.env
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p /tmp/myapp"
$SCP_CMD myapp/* $BOARD_USER@$BOARD_IP:/tmp/myapp/
$SSH_CMD $BOARD_USER@$BOARD_IP "cd /tmp/myapp && make"
```

交叉编译（PC 出 aarch64 二进制）需 SDK 自带 toolchain，路径因厂商 BSP 而异。

## 故障

| 现象 | 处理 |
|------|------|
| ping 不通 | 网线、同网段、板子已起 |
| Connection refused | sshd 未起 / 防火墙 |
| Permission denied | 密码或 `board.env` 错误 |
| `sshpass: not found` | `sudo apt install sshpass` |
| Text file busy | 先 `kill` 正在跑的旧二进制再 scp |
| No space left | `df -h`；清 `/tmp` 或大日志目录 |
