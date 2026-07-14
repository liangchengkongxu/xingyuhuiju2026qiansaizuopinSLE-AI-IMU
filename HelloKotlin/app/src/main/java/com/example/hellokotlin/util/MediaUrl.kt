package com.example.hellokotlin.util

import com.example.hellokotlin.BuildConfig

fun resolveMediaUrl(path: String?): String? {
    if (path.isNullOrBlank()) return null
    if (path.startsWith("http://") || path.startsWith("https://")) return path
    val base = BuildConfig.UPLOAD_BASE_URL.trimEnd('/')
    val relative = if (path.startsWith("/")) path else "/$path"
    return "$base$relative"
}
