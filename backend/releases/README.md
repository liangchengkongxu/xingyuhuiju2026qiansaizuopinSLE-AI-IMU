# OTA 发布说明

## 发布新版本步骤

1. 修改 `HelloKotlin/app/build.gradle.kts` 的 `versionCode` / `versionName`
2. 构建 APK：`gradlew assembleDebug`
3. 复制 APK 到 `backend/seed_assets/releases/xingyu-vX.Y-debug.apk`
4. 修改 `backend/releases/latest.json`：
   - `versionCode` 与 App 一致
   - `versionName` 与 App 一致
   - `apkUrl` 如 `/uploads/releases/xingyu-v0.11-debug.apk`
   - `changelog` 更新说明
5. 部署后端：`python .deploy/remote_deploy.py` 并重启 API
6. 用户 App「我的 → 检查更新」即可下载安装

## 接口

`GET http://47.107.120.9/api/v1/app/release`
