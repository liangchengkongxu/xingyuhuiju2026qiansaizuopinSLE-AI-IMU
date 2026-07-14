# SDK 集成说明

将本仓库 `bs20/` 接入 HiSilicon **fbb_bs2x** SDK。

## 步骤

### 1. 复制应用源码

```bash
cp -a bs20/application/paibing  /path/to/fbb_bs2x/src/application/bs20/
```

### 2. 应用 SDK Overlay

将 `sdk_overlay/` 内文件按相对路径复制到 SDK `src/`：

```bash
SDK=/path/to/fbb_bs2x/src
OVER=bs20/integration/sdk_overlay/src

cp -a $OVER/application/bs20/CMakeLists.txt  $SDK/application/bs20/
cp -a $OVER/build/config/target_config/bs20/config.py  $SDK/build/config/target_config/bs20/
# 蓝牙 target 还需：
cp -a $OVER/build/config/target_config/bs20/sign_config/standard_bs20_n1200_ble.cfg  $SDK/build/config/target_config/bs20/sign_config/
cp -a $OVER/build/config/target_config/bs20/menuconfig/acore/standard_bs20_n1200_ble.config  $SDK/build/config/target_config/bs20/menuconfig/acore/
```

### 3. 构建脚本

```bash
cp bs20/tools/*.sh  /path/to/fbb_bs2x/tools/
chmod +x /path/to/fbb_bs2x/tools/build_paibing_*.sh /path/to/fbb_bs2x/tools/setup_paibing_ble_prebuild.sh
```

### 4. config.py 变更摘要

- `standard-bs20-n1200`：`ram_component` 使用 `paibing_common` + `paibing_sle_porting`
- 新增 `standard-bs20-n1200-ble` target：`paibing_common` + `paibing_ble_porting`

### 5. 编译

见 [`../README.md`](../README.md)。

## app_os_init.patch

若 SDK 版本与开发时不一致，可参考 `app_os_init.patch` 检查 `app_main` 弱符号是否需调整。
