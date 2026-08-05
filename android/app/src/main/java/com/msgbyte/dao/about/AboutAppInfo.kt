package com.msgbyte.dao.about

import android.content.Context
import androidx.annotation.StringRes
import com.msgbyte.dao.R

data class AboutAppInfo(
    val appVersion: String,
    val engineVersion: String,
)

enum class BundledLicense(
    @get:StringRes val titleRes: Int,
    val version: String,
    @get:StringRes val licenseRes: Int,
    val assetPath: String,
) {
    U_BLOCK_ORIGIN(
        titleRes = R.string.ublock_origin_name,
        version = "1.72.2",
        licenseRes = R.string.gpl_v3_license,
        assetPath = "extensions/ublock_origin/LICENSE.txt",
    ),
    KISS_TRANSLATOR(
        titleRes = R.string.kiss_translator_name,
        version = "2.0.29",
        licenseRes = R.string.gpl_v3_only_license,
        assetPath = "notices/kiss_translator_license.txt",
    ),
}

fun readAboutAppInfo(context: Context): AboutAppInfo {
    val version = runCatching {
        context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }.getOrNull().orEmpty()
    return AboutAppInfo(
        appVersion = version.ifBlank { context.getString(R.string.unknown_value) },
        engineVersion = context.getString(R.string.mozilla_components_version),
    )
}

fun readAssetText(context: Context, assetPath: String): Result<String> = runCatching {
    context.assets.open(assetPath).bufferedReader().use { it.readText() }
}

fun readBundledLicenseText(context: Context, license: BundledLicense): Result<String> =
    readAssetText(context, license.assetPath)
