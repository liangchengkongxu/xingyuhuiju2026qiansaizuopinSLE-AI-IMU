# SLE 链路吞吐量测试（通用）

基于 WS73 `libsle_host` 的 **SSAP Write No Response** 压测。

- 本板角色：**发送端（TX / GATT Client）**
- 对端需提供 GATT Server，服务/特征 UUID：`0x2222` / `0x2323`

## 部署

```bash
cd ss928_board_capability_eval
cp board.env.example board.env    # 改 BOARD_IP
# 配置 WS73_SDK（含 libsle_host.a）
bash scripts/check_board.sh
bash sle_throughput_test/deploy_board.sh
```

改对端 MAC：编辑 `sle_tp_common.h` 后重新 deploy。

## 板端运行

```bash
/opt/sample/ws73/sle_tp_run.sh 0
/opt/sample/ws73/sle_tp_run.sh 0 -s 251 -i 1000
/opt/sample/ws73/sle_tp_run.sh 1
```

| 参数 | 说明 |
|------|------|
| `-s bytes` | 每包载荷，默认 251，最大 251 |
| `-i ms` | 统计间隔，默认 1000ms |

## 链路参数（本端已拉满，实际以协商为准）

| 项 | 值 |
|----|-----|
| MCS | 12 |
| MTU | 1500 |
| payload | 251 |
| PHY 请求 | 4M（对端常协商为 1M） |
| TX power | 20 dBm |

## 可选：本板做接收端

```bash
DEPLOY_SERVER=1 bash sle_throughput_test/deploy_board.sh
/opt/sample/ws73/sle_tp_server -i 1000
```

更多排错见上级目录 `docs/03-星闪WS73测评.md`。
