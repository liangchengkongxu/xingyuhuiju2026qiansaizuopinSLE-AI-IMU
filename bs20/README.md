# BS20 拍柄固件（星闪 / 蓝牙）

基于 **BS20-N1200** + **MPU-9250** 的乒乓球/羽毛球拍柄惯性传感节点。

## 功能

| 版本 | 编译 Target | 通信方式 |
|------|-------------|----------|
| 星闪 | `standard-bs20-n1200` | 非连接扫播，**22B 二进制** @ 100ms |
| 蓝牙 | `standard-bs20-n1200-ble` | 连接 + GATT Notify ASCII 行 |

## 目录

```
bs20/
├── application/paibing/   # 核心源码
│   ├── common/            # IMU、主循环
│   ├── sle/               # 星闪广播
│   └── ble/               # 蓝牙 Notify
├── tools/                 # 一键构建脚本（放到 SDK 根目录 tools/ 使用）
├── docs/                  # 主控解析说明
└── integration/           # 接入 HiSilicon SDK 的 overlay 文件
    └── sdk_overlay/       # 复制到 fbb_bs2x/src/ 对应路径
```

## 集成到 BS2X SDK

1. 下载 [fbb_bs2x](https://gitee.com/HiSpark/fbb_bs2x) SDK
2. 复制 `application/paibing/` → `src/application/bs20/paibing/`
3. 复制 `integration/sdk_overlay/` 下文件到 SDK `src/` 对应路径（覆盖/合并）
4. 复制 `tools/*.sh` → SDK 根目录 `tools/`
5. 详见 [`integration/SDK集成说明.md`](integration/SDK集成说明.md)

## 编译

在 SDK 根目录：

```bash
# 星闪版（可选 MAC 末字节 01~06）
bash tools/build_paibing_sle.sh 04

# 蓝牙版
bash tools/setup_paibing_ble_prebuild.sh
bash tools/build_paibing_ble.sh
```

烧录包：`src/tools/pkg/fwpkg/bs20/paibing_sle_macxx_all.fwpkg`（需本地编译生成，不含在 Git 中）

**勿烧录** `fota.fwpkg`（OTA 包，串口会超时）。

## 硬件

| 信号 | 引脚 |
|------|------|
| UART 调试 | MGPIO19/20, 115200 |
| I2C (MPU-9250) | SCL=MGPIO16, SDA=MGPIO15 |
| 复位键 | MGPIO22 |

## MAC 配置

编辑 `application/paibing/sle/mac_config.h` 中 `PAIBING_LOCAL_MAC_B5`（默认前缀 `cc:ad:c9:00:22:xx`）。
