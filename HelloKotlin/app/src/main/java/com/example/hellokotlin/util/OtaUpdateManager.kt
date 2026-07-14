package com.example.hellokotlin.util

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import com.example.hellokotlin.BuildConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File

data class AppReleaseInfo(
    val versionCode: Int,
    val versionName: String,
    val apkUrl: String,
    val changelog: String,
    val forceUpdate: Boolean
) {
    fun isNewerThanInstalled(): Boolean = versionCode > BuildConfig.VERSION_CODE
}

object OtaUpdateManager {
    private val http = OkHttpClient.Builder().build()

    fun installedVersionName(): String = BuildConfig.VERSION_NAME

    fun installedVersionCode(): Int = BuildConfig.VERSION_CODE

    fun resolveApkUrl(path: String): String = resolveMediaUrl(path).orEmpty()

    suspend fun downloadApk(context: Context, apkUrl: String): Result<File> = withContext(Dispatchers.IO) {
        runCatching {
            val url = resolveApkUrl(apkUrl)
            if (url.isBlank()) throw IllegalArgumentException("无效的下载地址")
            val request = Request.Builder().url(url).get().build()
            val response = http.newCall(request).execute()
            if (!response.isSuccessful) throw IllegalStateException("下载失败 (${response.code})")
            val body = response.body ?: throw IllegalStateException("下载内容为空")
            val dir = File(context.cacheDir, "ota").apply { mkdirs() }
            val file = File(dir, "xingyu-update.apk")
            body.byteStream().use { input ->
                file.outputStream().use { output -> input.copyTo(output) }
            }
            file
        }
    }

    fun canInstallPackages(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return true
        return context.packageManager.canRequestPackageInstalls()
    }

    fun openInstallPermissionSettings(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val intent = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).apply {
                data = Uri.parse("package:${context.packageName}")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        }
    }

    fun installApk(context: Context, apkFile: File): Result<Unit> = runCatching {
        val uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            apkFile
        )
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package.archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    }
}
