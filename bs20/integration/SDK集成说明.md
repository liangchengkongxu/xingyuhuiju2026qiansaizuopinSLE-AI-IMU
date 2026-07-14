# SDK 集成说明

本应用基于 HiSilicon **fbb_bs2x**（BS20-N1200）SDK 开发。提交包中的 `source/paibing/` 需放入 SDK 对应位置后方可编译。

---

## 1. 拷贝源码

```bash
cp -a paibing_submission/source/paibing  fbb_bs2x/src/application/bs20/
cp paibing_submission/tools/*.sh         fbb_bs2x/tools/
chmod +x fbb_bs2x/tools/build_paibing_*.sh fbb_bs2x/tools/setup_paibing_ble_prebuild.sh
```

---

## 2. CMake 入口

确认 `src/application/bs20/CMakeLists.txt` 包含：

```cmake
add_subdirectory(paibing)
```

---

## 3. 编译 Target（config.py）

在 `src/build/config/target_config/bs20/config.py` 中注册两个 target：

### 星闪版 `standard-bs20-n1200`

`ram_component` 中加入：

```python
'samples', 'paibing_common', 'paibing_sle_porting',
```

### 蓝牙版 `standard-bs20-n1200-ble`

`ram_component` 中加入：

```python
'samples', 'paibing_common', 'paibing_ble_porting',
```

其余 ROM/RAM、NV、sign 配置与 `standard-bs20-n1200` 模板一致。蓝牙 target 还需 sign 配置 `standard_bs20_n1200_ble.cfg`。

---

## 4. 应用入口

拍柄通过弱符号 `app_main()` 接管应用线程（定义在 `sle/paibing_app.c` 或 `ble/paibing_app.c`），无需修改 `app_os_init.c`。

SDK 默认 `app_main` 为 weak；拍柄组件链接后覆盖该符号。

---

## 5. 依赖的 SDK 示例组件

| 版本 | 依赖 samples 模块 |
|------|-------------------|
| 星闪 | SLE device discovery / connection manager |
| 蓝牙 | `ble_uart_server`、`ble_uart_server_adv` |

蓝牙版构建前需执行：

```bash
bash tools/setup_paibing_ble_prebuild.sh
```

该脚本为 `standard-bs20-n1200-ble` 建立与 n1200 相同的预编译库符号链接，并独立 fwpkg 输出目录。

---

## 6. 编译命令

```bash
cd fbb_bs2x/src

# 星闪
python3 build.py standard-bs20-n1200

# 蓝牙（先 setup）
bash ../tools/setup_paibing_ble_prebuild.sh
python3 build.py standard-bs20-n1200-ble
```

或使用根目录快捷脚本（会自动复制 fwpkg）：

```bash
bash tools/build_paibing_sle.sh [MAC末字节]
bash tools/build_paibing_ble.sh
```

---

## 7. 输出产物

| 产物 | 路径 |
|------|------|
| ELF | `output/bs20/acore/standard-bs20-n1200/application.elf` |
| 全量烧录包 | `output/bs20/fwpkg/standard-bs20-n1200/bs20_all_in_one.fwpkg` |
| 作品命名副本 | `tools/pkg/fwpkg/bs20/paibing_sle_macxx_all.fwpkg` |

---

## 8. 关键配置头文件

| 文件 | 用途 |
|------|------|
| `sle/mac_config.h` | 星闪 MAC、广播模式、多人模式开关 |
| `ble/mac_config.h` | 蓝牙 MAC、连接策略 |

常用宏：

```c
#define PAIBING_USE_FIXED_LOCAL_MAC 1      // 固定 MAC
#define PAIBING_SLE_SENSOR_BROADCAST 1     // 星闪：非连接扫播
#define PAIBING_BLE_SENSOR_BROADCAST 0     // 蓝牙：连接 Notify
```
