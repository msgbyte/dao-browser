package com.msgbyte.dao.ui

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Shield
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.AmoAddon
import com.msgbyte.dao.browser.AmoStoreState
import com.msgbyte.dao.browser.ExtensionInstallState
import com.msgbyte.dao.ui.theme.LocalNovaColors
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
fun AmoStoreScreen(
    state: AmoStoreState,
    installedExtensionGuids: Set<String>,
    installState: ExtensionInstallState,
    transientInstallGuid: String?,
    onTransientInstallGuidChange: (String?) -> Unit,
    onSearch: (String) -> Unit,
    onRetry: () -> Unit,
    onLoadNext: () -> Unit,
    onInstall: (AmoAddon) -> Boolean,
    onDismissInstallResult: () -> Unit,
    onResolvePermission: (Boolean) -> Unit,
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val stateQuery = when (state) {
        is AmoStoreState.Content -> state.query
        is AmoStoreState.Empty -> state.query
        is AmoStoreState.Failed -> state.query
        AmoStoreState.Loading -> ""
    }
    var searchText by remember(stateQuery) { mutableStateOf(stateQuery) }
    var selectedGuid by remember { mutableStateOf<String?>(null) }
    val installActive = installState is ExtensionInstallState.Staging ||
        installState is ExtensionInstallState.Installing ||
        installState is ExtensionInstallState.AwaitingPermission

    Column(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .background(colors.background)
            .testTag("amo-store-screen"),
    ) {
        ScreenHeader(stringResource(R.string.amo_store_title), onBack)
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedTextField(
                value = searchText,
                onValueChange = { searchText = it },
                modifier = Modifier.weight(1f).testTag("amo-search-field"),
                placeholder = { Text(stringResource(R.string.amo_search_hint)) },
                singleLine = true,
                shape = RoundedCornerShape(14.dp),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(
                    onSearch = { onSearch(searchText.trim()) },
                ),
            )
            StoreActionButton(
                label = stringResource(R.string.amo_search_action),
                enabled = true,
                modifier = Modifier.testTag("amo-search-submit"),
                onClick = { onSearch(searchText.trim()) },
            )
        }

        if (
            installState !is ExtensionInstallState.Idle &&
            installState !is ExtensionInstallState.AwaitingPermission
        ) {
            Box(Modifier.padding(horizontal = 16.dp)) {
                ExtensionInstallStatus(
                    state = installState,
                    onDismiss = onDismissInstallResult,
                )
            }
        }

        when (state) {
            AmoStoreState.Loading -> Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                CircularProgressIndicator(color = colors.accent)
            }

            is AmoStoreState.Empty -> AmoCatalogMessage(
                message = stringResource(R.string.amo_empty),
                onRetry = onRetry,
            )

            is AmoStoreState.Failed -> AmoCatalogMessage(
                message = stringResource(R.string.amo_failure),
                onRetry = onRetry,
            )

            is AmoStoreState.Content -> LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                if (state.query.isBlank()) {
                    item { SectionLabel(stringResource(R.string.amo_recommended)) }
                }
                items(state.items, key = AmoAddon::guid) { addon ->
                    val transientInstalled = transientInstallGuid == addon.guid &&
                        (installState is ExtensionInstallState.Installed ||
                            installState is ExtensionInstallState.Idle)
                    AmoAddonCard(
                        addon = addon,
                        expanded = selectedGuid == addon.guid,
                        installed = addon.guid in installedExtensionGuids || transientInstalled,
                        installing = transientInstallGuid == addon.guid && installActive,
                        installEnabled = !installActive &&
                            transientInstallGuid != addon.guid &&
                            addon.guid !in installedExtensionGuids,
                        onSelect = { selectedGuid = addon.guid },
                        onInstall = {
                            if (onInstall(addon)) onTransientInstallGuidChange(addon.guid)
                        },
                    )
                }
                if (state.nextPage != null) {
                    item {
                        StoreActionButton(
                            label = stringResource(R.string.amo_load_more),
                            enabled = !state.isLoadingNext,
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 4.dp)
                                .testTag("amo-load-more"),
                            onClick = onLoadNext,
                        )
                    }
                }
            }
        }
    }

    ExtensionPermissionSheet(
        state = installState,
        onResolvePermission = onResolvePermission,
    )
}

@Composable
private fun AmoAddonCard(
    addon: AmoAddon,
    expanded: Boolean,
    installed: Boolean,
    installing: Boolean,
    installEnabled: Boolean,
    onSelect: () -> Unit,
    onInstall: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val ratingLocale = LocalConfiguration.current.locales[0]
    NovaCard(
        modifier = Modifier
            .fillMaxWidth()
            .testTag("amo-addon-${addon.guid}")
            .clickable(onClick = onSelect),
    ) {
        Column(Modifier.fillMaxWidth().padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                AmoAddonIcon(addon.iconUrl)
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        text = addon.name,
                        color = colors.foreground,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (addon.author.isNotBlank()) {
                        Text(
                            text = stringResource(R.string.amo_author, addon.author),
                            color = colors.muted,
                            fontSize = 12.sp,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
                Spacer(Modifier.width(8.dp))
                StoreActionButton(
                    label = stringResource(
                        when {
                            installed -> R.string.amo_installed
                            installing -> R.string.amo_installing
                            else -> R.string.amo_install
                        },
                    ),
                    enabled = installEnabled,
                    modifier = Modifier.testTag("amo-install-${addon.guid}"),
                    onClick = onInstall,
                )
            }
            if (expanded) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 12.dp)
                        .testTag("amo-addon-detail-${addon.guid}"),
                    verticalArrangement = Arrangement.spacedBy(5.dp),
                ) {
                    if (addon.summary.isNotBlank()) {
                        Text(addon.summary, color = colors.foreground, fontSize = 13.sp)
                    }
                    Text(
                        stringResource(R.string.amo_version, addon.version),
                        color = colors.muted,
                        fontSize = 12.sp,
                    )
                    addon.rating?.let { rating ->
                        Text(
                            stringResource(
                                R.string.amo_rating,
                                String.format(ratingLocale, "%.1f", rating),
                            ),
                            color = colors.muted,
                            fontSize = 12.sp,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun AmoAddonIcon(iconUrl: String?) {
    val colors = LocalNovaColors.current
    val bitmap by produceState<Bitmap?>(initialValue = null, key1 = iconUrl) {
        value = withContext(Dispatchers.IO) { loadAmoIcon(iconUrl) }
    }
    Box(
        modifier = Modifier
            .size(44.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(colors.surfaceSecondary)
            .border(1.dp, colors.border, RoundedCornerShape(12.dp)),
        contentAlignment = Alignment.Center,
    ) {
        if (bitmap != null) {
            Image(
                bitmap = bitmap!!.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.size(34.dp).clip(RoundedCornerShape(8.dp)),
            )
        } else {
            Icon(Lucide.Shield, contentDescription = null, modifier = Modifier.size(22.dp))
        }
    }
}

@Composable
private fun AmoCatalogMessage(message: String, onRetry: () -> Unit) {
    val colors = LocalNovaColors.current
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(message, color = colors.muted, fontSize = 14.sp)
        Spacer(Modifier.height(12.dp))
        StoreActionButton(
            label = stringResource(R.string.amo_retry),
            enabled = true,
            modifier = Modifier.testTag("amo-retry"),
            onClick = onRetry,
        )
    }
}

@Composable
private fun StoreActionButton(
    label: String,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val colors = LocalNovaColors.current
    Surface(
        modifier = modifier
            .heightIn(min = 48.dp)
            .clip(RoundedCornerShape(12.dp))
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick),
        color = if (enabled) colors.accent else colors.surfaceSecondary,
        shape = RoundedCornerShape(12.dp),
        border = if (enabled) null else androidx.compose.foundation.BorderStroke(1.dp, colors.border),
    ) {
        Box(Modifier.padding(horizontal = 14.dp), contentAlignment = Alignment.Center) {
            Text(
                text = label,
                color = if (enabled) colors.onAccent else colors.muted,
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold,
            )
        }
    }
}

private fun loadAmoIcon(iconUrl: String?): Bitmap? {
    val url = runCatching { URL(iconUrl) }.getOrNull()
        ?.takeIf { it.protocol == "https" && it.host.isNotBlank() }
        ?: return null
    val connection = runCatching { url.openConnection() as HttpURLConnection }.getOrNull()
        ?: return null
    return try {
        connection.connectTimeout = 5_000
        connection.readTimeout = 5_000
        connection.inputStream.use(BitmapFactory::decodeStream)
    } catch (_: Throwable) {
        null
    } finally {
        connection.disconnect()
    }
}
