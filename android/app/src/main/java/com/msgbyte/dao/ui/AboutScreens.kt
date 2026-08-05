package com.msgbyte.dao.ui

import android.content.Context
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.composables.icons.lucide.ChevronRight
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Scale
import com.msgbyte.dao.R
import com.msgbyte.dao.about.AboutAppInfo
import com.msgbyte.dao.about.BundledLicense
import com.msgbyte.dao.about.readBundledLicenseText
import com.msgbyte.dao.ui.theme.LocalNovaColors
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
fun AboutScreen(
    appInfo: AboutAppInfo,
    onOpenLicenses: () -> Unit,
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current

    Column(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .background(colors.background)
            .testTag("about-screen"),
    ) {
        ScreenHeader(stringResource(R.string.about), onBack)
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .testTag("about-content"),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(Modifier.height(24.dp))
            Box(
                modifier = Modifier.size(88.dp),
                contentAlignment = Alignment.Center,
            ) {
                androidx.compose.foundation.Image(
                    painter = painterResource(R.drawable.dao_brand_logo),
                    contentDescription = null,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize(),
                )
            }
            Spacer(Modifier.height(12.dp))
            Text(
                text = stringResource(R.string.dao_browser),
                color = colors.foreground,
                fontSize = 22.sp,
                fontWeight = FontWeight.Bold,
            )
            Spacer(Modifier.height(24.dp))
            NovaCard(modifier = Modifier.fillMaxWidth()) {
                VersionRow(stringResource(R.string.app_version), appInfo.appVersion)
                RowDivider()
                VersionRow(
                    stringResource(R.string.browser_engine),
                    stringResource(R.string.gecko_engine_version, appInfo.engineVersion),
                )
            }
            Spacer(Modifier.height(14.dp))
            NovaCard(modifier = Modifier.fillMaxWidth()) {
                SettingsRow(
                    modifier = Modifier.testTag("open-source-licenses-entry"),
                    icon = Lucide.Scale,
                    title = stringResource(R.string.open_source_licenses),
                    onClick = onOpenLicenses,
                ) {
                    Icon(
                        Lucide.ChevronRight,
                        contentDescription = null,
                        tint = colors.faint,
                        modifier = Modifier.size(17.dp),
                    )
                }
            }
        }
    }
}

@Composable
fun OpenSourceLicensesScreen(
    onBack: () -> Unit,
    loadLicenseText: suspend (Context, BundledLicense) -> Result<String> = ::readBundledLicenseText,
) {
    var selectedLicense by rememberSaveable { mutableStateOf<BundledLicense?>(null) }

    BackHandler(enabled = selectedLicense != null) {
        selectedLicense = null
    }

    if (selectedLicense == null) {
        LicenseList(onBack = onBack, onSelectLicense = { selectedLicense = it })
    } else {
        LicenseDetail(
            license = requireNotNull(selectedLicense),
            onBack = { selectedLicense = null },
            loadLicenseText = loadLicenseText,
        )
    }
}

@Composable
private fun VersionRow(label: String, version: String) {
    val colors = LocalNovaColors.current
    androidx.compose.foundation.layout.Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, color = colors.muted, fontSize = 14.sp)
        Text(version, color = colors.foreground, fontSize = 14.sp, fontWeight = FontWeight.Medium)
    }
}

@Composable
private fun LicenseList(
    onBack: () -> Unit,
    onSelectLicense: (BundledLicense) -> Unit,
) {
    val colors = LocalNovaColors.current

    Column(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .background(colors.background)
            .testTag("open-source-licenses-screen"),
    ) {
        ScreenHeader(stringResource(R.string.open_source_licenses), onBack)
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
        ) {
            item { SectionLabel(stringResource(R.string.bundled_extension_licenses)) }
            item {
                NovaCard {
                    BundledLicense.entries.forEachIndexed { index, license ->
                        if (index > 0) RowDivider()
                        SettingsRow(
                            modifier = Modifier.testTag("bundled-license-${license.name.lowercase()}"),
                            icon = Lucide.Scale,
                            title = stringResource(license.titleRes),
                            subtitle = stringResource(
                                R.string.extension_license_summary,
                                license.version,
                                stringResource(license.licenseRes),
                            ),
                            onClick = { onSelectLicense(license) },
                        ) {
                            Icon(
                                Lucide.ChevronRight,
                                contentDescription = null,
                                tint = colors.faint,
                                modifier = Modifier.size(17.dp),
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun LicenseDetail(
    license: BundledLicense,
    onBack: () -> Unit,
    loadLicenseText: suspend (Context, BundledLicense) -> Result<String>,
) {
    val context = LocalContext.current
    val colors = LocalNovaColors.current
    val unavailable = stringResource(R.string.license_unavailable)
    var licenseText by remember(license) { mutableStateOf<String?>(null) }

    LaunchedEffect(license, loadLicenseText) {
        licenseText = withContext(Dispatchers.IO) {
            try {
                loadLicenseText(context, license).getOrElse { unavailable }
            } catch (error: CancellationException) {
                throw error
            } catch (_: Throwable) {
                unavailable
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .background(colors.background)
            .testTag("license-detail-screen"),
    ) {
        ScreenHeader(stringResource(license.titleRes), onBack)
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            SelectionContainer {
                Text(
                    text = licenseText.orEmpty(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 24.dp),
                    color = colors.foreground,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 13.sp,
                )
            }
        }
    }
}
