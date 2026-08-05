package com.msgbyte.dao.ui

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.composables.icons.lucide.AlignLeft
import com.composables.icons.lucide.BookOpen
import com.composables.icons.lucide.ChevronRight
import com.composables.icons.lucide.Ellipsis
import com.composables.icons.lucide.Eye
import com.composables.icons.lucide.Folder
import com.composables.icons.lucide.FolderPlus
import com.composables.icons.lucide.Globe
import com.composables.icons.lucide.Info
import com.composables.icons.lucide.Lock
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Moon
import com.composables.icons.lucide.Pause
import com.composables.icons.lucide.Play
import com.composables.icons.lucide.Plus
import com.composables.icons.lucide.Search
import com.composables.icons.lucide.Shield
import com.composables.icons.lucide.Star
import com.composables.icons.lucide.RefreshCw
import com.composables.icons.lucide.Trash2
import com.composables.icons.lucide.X
import com.composables.icons.lucide.Type
import com.composables.icons.lucide.Usb
import com.msgbyte.dao.R
import com.msgbyte.dao.about.readAboutAppInfo
import com.msgbyte.dao.browser.BookmarkEntry
import com.msgbyte.dao.browser.BookmarkFolder
import com.msgbyte.dao.browser.BookmarkKind
import com.msgbyte.dao.browser.HistoryVisit
import com.msgbyte.dao.browser.BrowserDownload
import com.msgbyte.dao.browser.DownloadStatus
import com.msgbyte.dao.browser.BrowserFontScale
import com.msgbyte.dao.browser.BrowserPreferenceState
import com.msgbyte.dao.browser.BrowserSearchEngine
import com.msgbyte.dao.browser.ExtensionRepository
import com.msgbyte.dao.browser.ExtensionInstallFailure
import com.msgbyte.dao.browser.ExtensionInstallState
import com.msgbyte.dao.browser.ExtensionPackageException
import com.msgbyte.dao.browser.ExtensionPackageStager
import com.msgbyte.dao.browser.InstalledExtension
import com.msgbyte.dao.browser.SystemDownloadRepository
import com.msgbyte.dao.ui.theme.LocalNovaColors
import java.net.URI
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.util.Locale
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

const val BOOKMARK_OPTIONS_TEST_TAG_PREFIX = "bookmark-options-"

@Composable
fun SettingsScreen(
    preferences: BrowserPreferenceState,
    onDarkThemeChange: (Boolean) -> Unit,
    onFontScaleChange: (BrowserFontScale) -> Unit,
    onSearchEngineChange: (BrowserSearchEngine) -> Unit,
    onTrackingProtectionChange: (Boolean) -> Unit,
    onDefaultPrivateBrowsingChange: (Boolean) -> Unit,
    onRemoteDebuggingChange: (Boolean) -> Unit,
    onEnableRemoteDebuggingWithAcknowledgement: () -> Unit,
    onOpenAbout: () -> Unit = {},
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val context = LocalContext.current
    val previewTypography = fontPreviewTypography(preferences.fontScale)
    var searchEngineMenuOpen by remember { mutableStateOf(false) }
    var remoteDebuggingWarningOpen by remember { mutableStateOf(false) }
    var remoteDebuggingGuideOpen by remember { mutableStateOf(false) }

    Column(
        Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).background(colors.background),
    ) {
        ScreenHeader(stringResource(R.string.settings), onBack)
        LazyColumn(
            modifier = Modifier.fillMaxSize().testTag("settings-list"),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp),
        ) {
            item { SectionLabel(stringResource(R.string.appearance)) }
            item {
                NovaCard {
                    SettingsRow(
                        icon = Lucide.Moon,
                        title = stringResource(R.string.dark_mode),
                        subtitle = stringResource(R.string.dark_mode_summary),
                    ) { NovaSwitch(preferences.darkTheme, onDarkThemeChange) }
                    RowDivider()
                    SettingsRow(icon = Lucide.Type, title = stringResource(R.string.font_size)) {
                        Box(Modifier.width(150.dp)) {
                            SegmentedControl(
                                listOf(stringResource(R.string.small), stringResource(R.string.medium), stringResource(R.string.large)),
                                preferences.fontScale.ordinal,
                                { onFontScaleChange(BrowserFontScale.entries[it]) },
                            )
                        }
                    }
                    RowDivider()
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 14.dp)
                            .background(colors.surfaceSecondary, RoundedCornerShape(12.dp))
                            .border(1.dp, colors.border, RoundedCornerShape(12.dp))
                            .padding(horizontal = 16.dp, vertical = 14.dp),
                    ) {
                        Text(
                            text = stringResource(R.string.font_preview_title),
                            color = colors.foreground,
                            fontSize = previewTypography.titleSizeSp.sp,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Spacer(Modifier.height(5.dp))
                        Text(
                            text = stringResource(R.string.font_preview_body),
                            color = colors.muted,
                            fontSize = previewTypography.bodySizeSp.sp,
                        )
                    }
                }
            }
            item { SectionLabel(stringResource(R.string.search_and_privacy)) }
            item {
                NovaCard {
                    SettingsRow(
                        icon = Lucide.Search,
                        title = stringResource(R.string.default_search_engine),
                        onClick = { searchEngineMenuOpen = true },
                    ) {
                        Box {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Text(
                                    preferences.searchEngine.displayName(),
                                    color = colors.muted,
                                    fontSize = 13.5.sp,
                                )
                                Icon(Lucide.ChevronRight, null, tint = colors.faint, modifier = Modifier.size(17.dp))
                            }
                            DropdownMenu(
                                expanded = searchEngineMenuOpen,
                                onDismissRequest = { searchEngineMenuOpen = false },
                            ) {
                                BrowserSearchEngine.entries.forEach { engine ->
                                    DropdownMenuItem(
                                        text = { Text(engine.displayName()) },
                                        onClick = {
                                            onSearchEngineChange(engine)
                                            searchEngineMenuOpen = false
                                        },
                                    )
                                }
                            }
                        }
                    }
                    RowDivider()
                    SettingsRow(
                        icon = Lucide.Shield,
                        title = stringResource(R.string.block_tracking),
                        subtitle = stringResource(R.string.block_tracking_summary),
                    ) { NovaSwitch(preferences.trackingProtectionEnabled, onTrackingProtectionChange) }
                    RowDivider()
                    SettingsRow(
                        icon = Lucide.Eye,
                        title = stringResource(R.string.private_browsing),
                        subtitle = stringResource(R.string.private_browsing_summary),
                    ) {
                        NovaSwitch(preferences.defaultPrivateBrowsing, onDefaultPrivateBrowsingChange)
                    }
                }
            }
            item { SectionLabel(stringResource(R.string.developer_tools)) }
            item {
                NovaCard {
                    SettingsRow(
                        icon = Lucide.Usb,
                        title = stringResource(R.string.usb_remote_debugging),
                        subtitle = stringResource(
                            if (preferences.remoteDebuggingEnabled) {
                                R.string.usb_remote_debugging_enabled_summary
                            } else {
                                R.string.usb_remote_debugging_disabled_summary
                            },
                        ),
                        subtitleColor = if (preferences.remoteDebuggingEnabled) colors.accent else null,
                    ) {
                        NovaSwitch(
                            checked = preferences.remoteDebuggingEnabled,
                            onCheckedChange = { requestedEnabled ->
                                when {
                                    !requestedEnabled -> onRemoteDebuggingChange(false)
                                    preferences.remoteDebuggingWarningAcknowledged ->
                                        onRemoteDebuggingChange(true)
                                    else -> remoteDebuggingWarningOpen = true
                                }
                            },
                            modifier = Modifier.testTag("usb-remote-debugging-switch"),
                        )
                    }
                    RowDivider()
                    SettingsRow(
                        modifier = Modifier.testTag("settings-connection-guide-entry"),
                        icon = Lucide.BookOpen,
                        title = stringResource(R.string.remote_debugging_connection_guide),
                        onClick = { remoteDebuggingGuideOpen = true },
                    ) {
                        Icon(
                            Lucide.ChevronRight,
                            null,
                            tint = colors.faint,
                            modifier = Modifier.size(17.dp),
                        )
                    }
                }
            }
            item { SectionLabel(stringResource(R.string.about)) }
            item {
                NovaCard {
                    SettingsRow(
                        modifier = Modifier.testTag("settings-about-entry"),
                        icon = Lucide.Info,
                        title = stringResource(R.string.about_dao_browser),
                        onClick = onOpenAbout,
                    ) {
                        Text(
                            text = readAboutAppInfo(context).appVersion,
                            color = colors.muted,
                            fontSize = 13.5.sp,
                            modifier = Modifier.testTag("settings-about-version"),
                        )
                        Icon(
                            Lucide.ChevronRight,
                            null,
                            tint = colors.faint,
                            modifier = Modifier.size(17.dp),
                        )
                    }
                }
            }
        }
    }
    if (remoteDebuggingWarningOpen) {
        AlertDialog(
            onDismissRequest = { remoteDebuggingWarningOpen = false },
            title = { Text(stringResource(R.string.remote_debugging_warning_title)) },
            text = { Text(stringResource(R.string.remote_debugging_warning_body)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        remoteDebuggingWarningOpen = false
                        onEnableRemoteDebuggingWithAcknowledgement()
                    },
                ) { Text(stringResource(R.string.enable_remote_debugging)) }
            },
            dismissButton = {
                TextButton(onClick = { remoteDebuggingWarningOpen = false }) {
                    Text(stringResource(R.string.cancel))
                }
            },
        )
    }
    if (remoteDebuggingGuideOpen) {
        AlertDialog(
            onDismissRequest = { remoteDebuggingGuideOpen = false },
            title = { Text(stringResource(R.string.remote_debugging_guide_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(stringResource(R.string.remote_debugging_guide_step_1))
                    Text(stringResource(R.string.remote_debugging_guide_step_2))
                    Text(stringResource(R.string.remote_debugging_guide_step_3))
                    Text(
                        stringResource(R.string.remote_debugging_desktop_note),
                        color = colors.muted,
                        fontSize = 12.sp,
                    )
                    TextButton(onClick = { openDeveloperOptions(context) }) {
                        Text(stringResource(R.string.open_developer_options))
                    }
                    TextButton(onClick = { copyAboutDebuggingAddress(context) }) {
                        Text(stringResource(R.string.copy_about_debugging))
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { remoteDebuggingGuideOpen = false }) {
                    Text(stringResource(R.string.done))
                }
            },
        )
    }
}

@Composable
private fun BrowserSearchEngine.displayName(): String = stringResource(
    when (this) {
        BrowserSearchEngine.GOOGLE -> R.string.google
        BrowserSearchEngine.BAIDU -> R.string.baidu
        BrowserSearchEngine.BING -> R.string.bing
        BrowserSearchEngine.DUCKDUCKGO -> R.string.duckduckgo
    },
)

@Composable
internal fun SettingsRow(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    title: String,
    subtitle: String = "",
    subtitleColor: Color? = null,
    onClick: (() -> Unit)? = null,
    trailing: @Composable () -> Unit,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(horizontal = 16.dp, vertical = 10.dp)
            .height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(32.dp).background(colors.surfaceSecondary, RoundedCornerShape(9.dp)).border(1.dp, colors.border, RoundedCornerShape(9.dp)),
            contentAlignment = Alignment.Center,
        ) { Icon(icon, null, modifier = Modifier.size(18.dp)) }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.Medium)
            if (subtitle.isNotEmpty()) Text(subtitle, color = subtitleColor ?: colors.muted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) { trailing() }
    }
}

private fun openDeveloperOptions(context: Context) {
    try {
        context.startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS))
    } catch (_: ActivityNotFoundException) {
        context.startActivity(Intent(Settings.ACTION_SETTINGS))
    }
}

private fun copyAboutDebuggingAddress(context: Context) {
    val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return
    runCatching {
        clipboard.setPrimaryClip(
            ClipData.newPlainText(
                context.getString(R.string.remote_debugging_clip_label),
                "about:debugging",
            ),
        )
    }.onSuccess {
        Toast.makeText(
            context,
            context.getString(R.string.about_debugging_copied),
            Toast.LENGTH_SHORT,
        ).show()
    }
}

@Composable
fun HistoryScreen(
    visits: List<HistoryVisit>,
    onClear: () -> Unit,
    onDelete: (Long) -> Unit,
    onBack: () -> Unit,
    onNavigate: (String) -> Unit,
) {
    val colors = LocalNovaColors.current
    var query by remember { mutableStateOf("") }
    val filtered = remember(query, visits) {
        visits.filter { (it.title + " " + it.url).contains(query.trim(), ignoreCase = true) }
    }
    val todayLabel = stringResource(R.string.today)
    val yesterdayLabel = stringResource(R.string.yesterday_short)
    val datePattern = stringResource(R.string.history_date_format)
    val groups = remember(filtered, todayLabel, yesterdayLabel, datePattern) {
        groupHistoryVisits(filtered, todayLabel, yesterdayLabel, datePattern)
    }

    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).background(colors.background)) {
        ScreenHeader(stringResource(R.string.history), onBack) {
            Text(
                stringResource(R.string.clear),
                color = if (visits.isEmpty()) colors.faint else colors.danger,
                fontSize = 13.5.sp,
                fontWeight = FontWeight.Medium,
                modifier = Modifier.clickable(enabled = visits.isNotEmpty(), onClick = onClear).padding(12.dp),
            )
        }
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 2.dp).height(42.dp)
                .background(colors.surfaceSecondary, RoundedCornerShape(12.dp)).border(1.dp, colors.border, RoundedCornerShape(12.dp)).padding(horizontal = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(Lucide.Search, null, tint = colors.muted, modifier = Modifier.size(17.dp))
            Spacer(Modifier.width(9.dp))
            BasicTextField(
                value = query,
                onValueChange = { query = it },
                modifier = Modifier.weight(1f),
                singleLine = true,
                textStyle = androidx.compose.ui.text.TextStyle(color = colors.foreground, fontSize = 14.sp),
                decorationBox = { inner ->
                    if (query.isEmpty()) Text(stringResource(R.string.search_history), color = colors.faint, fontSize = 14.sp)
                    inner()
                },
            )
        }
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 24.dp)) {
            if (groups.isEmpty()) {
                item { EmptyLibraryMessage(stringResource(if (query.isBlank()) R.string.no_history else R.string.no_search_results)) }
            }
            groups.forEach { group ->
                HistoryGroup(group.label, group.visits, onNavigate, onDelete)
            }
        }
    }
}

private fun androidx.compose.foundation.lazy.LazyListScope.HistoryGroup(
    title: String,
    rows: List<HistoryVisit>,
    onNavigate: (String) -> Unit,
    onDelete: (Long) -> Unit,
) {
    stickyHeader { SectionLabel(title, Modifier.fillMaxWidth().background(LocalNovaColors.current.background)) }
    items(rows) { row ->
        ListRow(
            row.title,
            row.url,
            letter = siteLetter(row.url, row.title),
            avatarColor = Color(0xFF3F424A),
            meta = formatHistoryTime(Instant.ofEpochMilli(row.visitedAt).atZone(ZoneId.systemDefault())),
            onClick = { onNavigate(row.url) },
        )
    }
}

@Composable
fun BookmarksScreen(
    bookmarks: List<BookmarkEntry>,
    folders: List<BookmarkFolder>,
    onCreateFolder: (String) -> Unit = {},
    onUpdateBookmark: (Long, String, Long?, BookmarkKind) -> Unit = { _, _, _, _ -> },
    onDeleteBookmark: (Long) -> Unit = {},
    onBack: () -> Unit,
    onNavigate: (String) -> Unit,
) {
    val colors = LocalNovaColors.current
    var selected by remember { mutableStateOf(0) }
    var createFolderOpen by remember { mutableStateOf(false) }
    var folderTitle by remember { mutableStateOf("") }
    val kind = if (selected == 0) BookmarkKind.FAVORITE else BookmarkKind.READING_LIST
    val visibleBookmarks = bookmarks.filter { it.kind == kind }
    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).background(colors.background)) {
        ScreenHeader(stringResource(R.string.bookmarks), onBack) {
            IconButton(onClick = { createFolderOpen = true }, modifier = Modifier.size(44.dp)) {
                Icon(Lucide.FolderPlus, stringResource(R.string.new_folder), modifier = Modifier.size(21.dp))
            }
        }
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp)) {
            item {
                SegmentedControl(
                    listOf(stringResource(R.string.favorites), stringResource(R.string.reading_list)),
                    selected,
                    { selected = it },
                    Modifier.padding(bottom = 16.dp),
                )
            }
            if (selected == 0) {
                items(folders.chunked(2)) { rowFolders ->
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        rowFolders.forEach { folder ->
                            FolderCard(
                                folder.title,
                                stringResource(R.string.bookmark_count, bookmarks.count { it.folderId == folder.id }),
                                Modifier.weight(1f),
                            )
                        }
                        if (rowFolders.size == 1) Spacer(Modifier.weight(1f))
                    }
                }
                item { SectionLabel(stringResource(R.string.uncategorized)) }
            }
            if (visibleBookmarks.isEmpty()) {
                item { EmptyLibraryMessage(stringResource(R.string.no_bookmarks)) }
            } else {
                item {
                    NovaCard {
                        visibleBookmarks.forEachIndexed { index, row ->
                            ListRow(
                                row.title,
                                row.url,
                                letter = siteLetter(row.url, row.title),
                                avatarColor = Color(0xFF3F424A),
                                trailing = {
                                    BookmarkActions(
                                        bookmark = row,
                                        folders = folders,
                                        onUpdate = onUpdateBookmark,
                                        onDelete = onDeleteBookmark,
                                    )
                                },
                                onClick = { onNavigate(row.url) },
                            )
                            if (index != visibleBookmarks.lastIndex) RowDivider()
                        }
                    }
                }
            }
        }
    }
    if (createFolderOpen) {
        AlertDialog(
            onDismissRequest = { createFolderOpen = false },
            title = { Text(stringResource(R.string.new_folder)) },
            text = {
                OutlinedTextField(
                    value = folderTitle,
                    onValueChange = { folderTitle = it },
                    label = { Text(stringResource(R.string.folder_name)) },
                    singleLine = true,
                )
            },
            confirmButton = {
                TextButton(
                    enabled = folderTitle.isNotBlank(),
                    onClick = {
                        onCreateFolder(folderTitle)
                        folderTitle = ""
                        createFolderOpen = false
                    },
                ) { Text(stringResource(R.string.create)) }
            },
            dismissButton = {
                TextButton(onClick = { createFolderOpen = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

@Composable
private fun BookmarkActions(
    bookmark: BookmarkEntry,
    folders: List<BookmarkFolder>,
    onUpdate: (Long, String, Long?, BookmarkKind) -> Unit,
    onDelete: (Long) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    var renameOpen by remember { mutableStateOf(false) }
    var title by remember(bookmark.id, bookmark.title) { mutableStateOf(bookmark.title) }
    Box {
        IconButton(
            onClick = { expanded = true },
            modifier = Modifier.size(44.dp).testTag("$BOOKMARK_OPTIONS_TEST_TAG_PREFIX${bookmark.id}"),
        ) {
            Icon(
                Lucide.Ellipsis,
                stringResource(R.string.bookmark_options),
                tint = LocalNovaColors.current.faint,
                modifier = Modifier.size(18.dp),
            )
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.edit_bookmark)) },
                onClick = {
                    expanded = false
                    renameOpen = true
                },
            )
            DropdownMenuItem(
                text = {
                    Text(
                        stringResource(
                            if (bookmark.kind == BookmarkKind.FAVORITE) {
                                R.string.move_to_reading_list
                            } else {
                                R.string.move_to_favorites
                            },
                        ),
                    )
                },
                onClick = {
                    expanded = false
                    onUpdate(
                        bookmark.id,
                        bookmark.title,
                        bookmark.folderId,
                        if (bookmark.kind == BookmarkKind.FAVORITE) BookmarkKind.READING_LIST else BookmarkKind.FAVORITE,
                    )
                },
            )
            if (bookmark.folderId != null) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.remove_from_folder)) },
                    onClick = {
                        expanded = false
                        onUpdate(bookmark.id, bookmark.title, null, bookmark.kind)
                    },
                )
            }
            folders.filterNot { it.id == bookmark.folderId }.forEach { folder ->
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.move_to_folder, folder.title)) },
                    onClick = {
                        expanded = false
                        onUpdate(bookmark.id, bookmark.title, folder.id, bookmark.kind)
                    },
                )
            }
            DropdownMenuItem(
                text = { Text(stringResource(R.string.delete), color = LocalNovaColors.current.danger) },
                onClick = {
                    expanded = false
                    onDelete(bookmark.id)
                },
            )
        }
    }
    if (renameOpen) {
        AlertDialog(
            onDismissRequest = { renameOpen = false },
            title = { Text(stringResource(R.string.edit_bookmark)) },
            text = {
                OutlinedTextField(
                    value = title,
                    onValueChange = { title = it },
                    label = { Text(stringResource(R.string.bookmark_title)) },
                    singleLine = true,
                )
            },
            confirmButton = {
                TextButton(
                    enabled = title.isNotBlank(),
                    onClick = {
                        onUpdate(bookmark.id, title, bookmark.folderId, bookmark.kind)
                        renameOpen = false
                    },
                ) { Text(stringResource(R.string.save)) }
            },
            dismissButton = {
                TextButton(onClick = { renameOpen = false }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

@Composable
private fun FolderCard(title: String, count: String, modifier: Modifier = Modifier) {
    val colors = LocalNovaColors.current
    NovaCard(modifier.height(132.dp)) {
        Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.SpaceBetween) {
            Box(
                Modifier.size(38.dp).background(colors.surfaceSecondary, RoundedCornerShape(11.dp)).border(1.dp, colors.border, RoundedCornerShape(11.dp)),
                contentAlignment = Alignment.Center,
            ) { Icon(Lucide.Folder, null, modifier = Modifier.size(20.dp)) }
            Column {
                Text(title, color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.SemiBold)
                Text(count, color = colors.muted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }
}

@Composable
private fun EmptyLibraryMessage(message: String) {
    Text(
        message,
        modifier = Modifier.fillMaxWidth().padding(vertical = 36.dp),
        color = LocalNovaColors.current.faint,
        fontSize = 14.sp,
        textAlign = androidx.compose.ui.text.style.TextAlign.Center,
    )
}

private data class HistoryVisitGroup(val label: String, val visits: List<HistoryVisit>)

private fun groupHistoryVisits(
    visits: List<HistoryVisit>,
    todayLabel: String,
    yesterdayLabel: String,
    datePattern: String,
): List<HistoryVisitGroup> {
    val zone = ZoneId.systemDefault()
    val today = LocalDate.now(zone)
    return visits.groupBy { Instant.ofEpochMilli(it.visitedAt).atZone(zone).toLocalDate() }
        .map { (date, rows) ->
            val label = when (date) {
                today -> todayLabel
                today.minusDays(1) -> yesterdayLabel
                else -> formatHistoryDate(date, datePattern)
            }
            HistoryVisitGroup(label, rows)
        }
}

private fun siteLetter(url: String, title: String): String =
    title.trim().firstOrNull()?.uppercase()
        ?: runCatching { URI(url).host?.firstOrNull()?.uppercase() }.getOrNull()
        ?: "?"

@Composable
fun DownloadsScreen(repository: SystemDownloadRepository, onBack: () -> Unit) {
    val colors = LocalNovaColors.current
    val downloads by repository.downloads.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    var editMode by remember { mutableStateOf(false) }
    val active = downloads.filter { it.status != DownloadStatus.SUCCESSFUL }
    val complete = downloads.filter { it.status == DownloadStatus.SUCCESSFUL }
    LaunchedEffect(repository) {
        while (true) {
            runCatching { repository.refresh() }
            delay(1_000)
        }
    }
    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).background(colors.background)) {
        ScreenHeader(stringResource(R.string.downloads), onBack) {
            Text(
                stringResource(if (editMode) R.string.done else R.string.edit),
                color = if (downloads.isEmpty()) colors.faint else colors.muted,
                fontSize = 13.5.sp,
                modifier = Modifier.clickable(enabled = downloads.isNotEmpty()) { editMode = !editMode }.padding(12.dp),
            )
        }
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp)) {
            if (downloads.isEmpty()) {
                item { EmptyLibraryMessage(stringResource(R.string.no_downloads)) }
            }
            if (active.isNotEmpty()) {
                item { SectionLabel(stringResource(R.string.dao_in_progress)) }
                item {
                    NovaCard {
                        active.forEachIndexed { index, download ->
                            RealDownloadRow(
                                download = download,
                                editMode = editMode,
                                onCancel = { scope.launch { repository.cancel(download.id) } },
                                onRetry = { scope.launch { repository.retry(download.id) } },
                                onRemove = { scope.launch { repository.remove(download.id) } },
                                onOpen = {},
                            )
                            if (index != active.lastIndex) RowDivider()
                        }
                    }
                }
            }
            if (complete.isNotEmpty()) {
                item { SectionLabel(stringResource(R.string.completed)) }
                item {
                    NovaCard {
                        complete.forEachIndexed { index, download ->
                            RealDownloadRow(
                                download = download,
                                editMode = editMode,
                                onCancel = {},
                                onRetry = {},
                                onRemove = { scope.launch { repository.remove(download.id) } },
                                onOpen = {
                                    download.localUri?.let { rawUri ->
                                        val intent = Intent(Intent.ACTION_VIEW).apply {
                                            setDataAndType(Uri.parse(rawUri), download.request.contentType ?: "*/*")
                                            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                                        }
                                        try {
                                            context.startActivity(intent)
                                        } catch (_: ActivityNotFoundException) {
                                            // The file remains available in the system Downloads app.
                                        }
                                    }
                                },
                            )
                            if (index != complete.lastIndex) RowDivider()
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun RealDownloadRow(
    download: BrowserDownload,
    editMode: Boolean,
    onCancel: () -> Unit,
    onRetry: () -> Unit,
    onRemove: () -> Unit,
    onOpen: () -> Unit,
) {
    val action = when {
        editMode -> DownloadAction(Lucide.Trash2, stringResource(R.string.delete), onRemove)
        download.status == DownloadStatus.FAILED -> DownloadAction(Lucide.RefreshCw, stringResource(R.string.retry), onRetry)
        download.status == DownloadStatus.SUCCESSFUL -> DownloadAction(Lucide.ChevronRight, stringResource(R.string.open), onOpen)
        else -> DownloadAction(Lucide.X, stringResource(R.string.cancel_download), onCancel)
    }
    DownloadRow(
        type = download.request.fileName.substringAfterLast('.', stringResource(R.string.file_type)).uppercase().take(4),
        name = download.request.fileName,
        meta = downloadMeta(download),
        actionIcon = action.icon,
        actionDescription = action.description,
        progress = download.progress.takeUnless { download.status == DownloadStatus.FAILED },
        onAction = action.onClick,
    )
}

private data class DownloadAction(val icon: ImageVector, val description: String, val onClick: () -> Unit)

@Composable
private fun downloadMeta(download: BrowserDownload): String {
    val size = buildString {
        append(formatBytes(download.bytesDownloaded))
        download.totalBytes?.let { append(" / ").append(formatBytes(it)) }
    }
    val status = stringResource(
        when (download.status) {
            DownloadStatus.PENDING -> R.string.download_pending
            DownloadStatus.RUNNING -> R.string.downloading
            DownloadStatus.PAUSED -> R.string.download_waiting
            DownloadStatus.SUCCESSFUL -> R.string.download_complete
            DownloadStatus.FAILED -> R.string.download_failed
        },
    )
    return "$size · $status"
}

private fun formatBytes(bytes: Long): String {
    if (bytes < 1_024) return "$bytes B"
    val units = arrayOf("KB", "MB", "GB", "TB")
    var value = bytes.toDouble() / 1_024.0
    var unitIndex = 0
    while (value >= 1_024 && unitIndex < units.lastIndex) {
        value /= 1_024.0
        unitIndex++
    }
    return String.format(Locale.ROOT, "%.1f %s", value, units[unitIndex])
}

@Composable
private fun DownloadRow(
    type: String,
    name: String,
    meta: String,
    actionIcon: ImageVector,
    actionDescription: String,
    progress: Float? = null,
    onAction: () -> Unit = {},
) {
    val colors = LocalNovaColors.current
    Row(Modifier.fillMaxWidth().padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(44.dp).background(colors.surfaceSecondary, RoundedCornerShape(11.dp)).border(1.dp, colors.border, RoundedCornerShape(11.dp)),
            contentAlignment = Alignment.Center,
        ) { Text(type, color = colors.muted, fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.SemiBold) }
        Spacer(Modifier.width(13.dp))
        Column(Modifier.weight(1f)) {
            Text(name, color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(meta, color = colors.muted, fontSize = 12.sp, maxLines = 1)
            if (progress != null) {
                Spacer(Modifier.height(7.dp))
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.fillMaxWidth().height(5.dp),
                    color = colors.accent,
                    trackColor = colors.surfaceSecondary,
                    gapSize = 0.dp,
                    drawStopIndicator = {},
                )
            }
        }
        Spacer(Modifier.width(10.dp))
        Surface(
            modifier = Modifier.size(44.dp).clickable(onClick = onAction),
            shape = RoundedCornerShape(10.dp),
            color = colors.surfaceSecondary,
            border = BorderStroke(1.dp, colors.border),
        ) { Box(contentAlignment = Alignment.Center) { Icon(actionIcon, actionDescription, modifier = Modifier.size(18.dp)) } }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExtensionsScreen(
    repository: ExtensionRepository,
    onOpenStore: () -> Unit,
    onActionInvoked: () -> Unit = {},
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val extensions by repository.extensions.collectAsStateWithLifecycle()
    val installState by repository.installState.collectAsStateWithLifecycle()
    val stager = remember(context) { ExtensionPackageStager(context.applicationContext) }
    var pendingUninstall by remember { mutableStateOf<InstalledExtension?>(null) }
    val busyMessage = stringResource(R.string.extension_install_busy)
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        repository.setStaging()
        scope.launch {
            try {
                val staged = withContext(Dispatchers.IO) { stager.stage(uri) }
                if (!repository.install(staged)) {
                    staged.delete()
                    Toast.makeText(context, busyMessage, Toast.LENGTH_SHORT).show()
                }
            } catch (error: ExtensionPackageException) {
                repository.reportStagingFailure(error)
            }
        }
    }
    LaunchedEffect(repository) {
        repeat(3) {
            repository.refresh()
            if (repository.extensions.value.isNotEmpty()) return@LaunchedEffect
            delay(400)
        }
    }
    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing).background(colors.background)) {
        ScreenHeader(stringResource(R.string.extensions), onBack) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Surface(
                    modifier = Modifier
                        .height(36.dp)
                        .testTag("open-extension-store")
                        .clickable(onClick = onOpenStore),
                    color = colors.surfaceSecondary,
                    border = BorderStroke(1.dp, colors.border),
                    shape = RoundedCornerShape(10.dp),
                ) { Box(Modifier.padding(horizontal = 12.dp), contentAlignment = Alignment.Center) { Text(stringResource(R.string.extension_store), color = colors.foreground, fontSize = 13.sp, fontWeight = FontWeight.SemiBold) } }
                Surface(
                    modifier = Modifier.height(36.dp).testTag("install-extension").clickable {
                        picker.launch(
                            arrayOf(
                                "application/x-xpinstall",
                                "application/zip",
                                "application/octet-stream",
                            ),
                        )
                    },
                    color = colors.accent,
                    shape = RoundedCornerShape(10.dp),
                ) { Box(Modifier.padding(horizontal = 12.dp), contentAlignment = Alignment.Center) { Text(stringResource(R.string.install_xpi), color = colors.onAccent, fontSize = 13.sp, fontWeight = FontWeight.SemiBold) } }
            }
        }
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 24.dp)) {
            if (installState !is ExtensionInstallState.Idle &&
                installState !is ExtensionInstallState.AwaitingPermission
            ) {
                item {
                    ExtensionInstallStatus(
                        state = installState,
                        onDismiss = repository::dismissResult,
                    )
                }
            }
            item { SectionLabel(stringResource(R.string.installed_extensions, extensions.size)) }
            if (extensions.isEmpty()) {
                item { EmptyLibraryMessage(stringResource(R.string.no_extensions)) }
            } else {
                item {
                    NovaCard {
                        extensions.forEachIndexed { index, extension ->
                            ExtensionRow(
                                icon = Lucide.Shield,
                                title = extension.name,
                                subtitle = if (extension.description.isBlank()) {
                                    stringResource(R.string.extension_version, extension.version)
                                } else {
                                    extension.description
                                },
                            ) {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    if (extension.actionAvailable) {
                                        IconButton(
                                            onClick = {
                                                if (repository.invokeAction(extension.id)) {
                                                    onActionInvoked()
                                                }
                                            },
                                            modifier = Modifier
                                                .size(40.dp)
                                                .testTag("open-extension-${extension.id}"),
                                        ) {
                                            Icon(
                                                Lucide.ChevronRight,
                                                stringResource(R.string.open),
                                                modifier = Modifier.size(18.dp),
                                                tint = colors.muted,
                                            )
                                        }
                                    }
                                    if (!extension.builtIn) {
                                        IconButton(
                                            onClick = { pendingUninstall = extension },
                                            modifier = Modifier
                                                .size(40.dp)
                                                .testTag("uninstall-extension-${extension.id}"),
                                        ) {
                                            Icon(
                                                Lucide.Trash2,
                                                stringResource(R.string.uninstall_extension),
                                                modifier = Modifier.size(18.dp),
                                                tint = colors.muted,
                                            )
                                        }
                                    }
                                    NovaSwitch(extension.enabled, onCheckedChange = {
                                        repository.setEnabled(extension.id, it)
                                    })
                                }
                            }
                            if (index != extensions.lastIndex) RowDivider()
                        }
                    }
                }
            }
        }
    }

    ExtensionPermissionSheet(
        state = installState,
        onResolvePermission = repository::resolvePermission,
    )

    pendingUninstall?.let { extension ->
        AlertDialog(
            onDismissRequest = { pendingUninstall = null },
            title = { Text(stringResource(R.string.uninstall_extension_title)) },
            text = { Text(stringResource(R.string.uninstall_extension_message, extension.name)) },
            confirmButton = {
                TextButton(onClick = {
                    repository.uninstall(extension.id)
                    pendingUninstall = null
                }) { Text(stringResource(R.string.uninstall_extension)) }
            },
            dismissButton = {
                TextButton(onClick = { pendingUninstall = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ExtensionPermissionSheet(
    state: ExtensionInstallState,
    onResolvePermission: (Boolean) -> Unit,
) {
    val permission = (state as? ExtensionInstallState.AwaitingPermission)?.prompt ?: return
    val colors = LocalNovaColors.current
    ModalBottomSheet(
        onDismissRequest = { onResolvePermission(false) },
        containerColor = colors.background,
        modifier = Modifier.testTag("extension-permission-sheet"),
    ) {
        Column(Modifier.fillMaxWidth().padding(start = 24.dp, end = 24.dp, bottom = 28.dp)) {
            Text(stringResource(R.string.extension_permission_title), color = colors.foreground, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(8.dp))
            Text(stringResource(R.string.extension_identity, permission.name, permission.version), color = colors.muted, fontSize = 13.sp)
            Spacer(Modifier.height(16.dp))
            Text(stringResource(R.string.extension_permission_warning), color = colors.foreground, fontSize = 14.sp)
            ExtensionPermissionGroup(R.string.extension_browser_permissions, permission.permissions)
            ExtensionPermissionGroup(R.string.extension_site_access, permission.origins)
            ExtensionPermissionGroup(R.string.extension_data_collection, permission.dataCollectionPermissions)
            Spacer(Modifier.height(18.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(
                    onClick = { onResolvePermission(false) },
                    modifier = Modifier.testTag("cancel-extension"),
                ) { Text(stringResource(R.string.cancel)) }
                TextButton(
                    onClick = { onResolvePermission(true) },
                    modifier = Modifier.testTag("approve-extension"),
                ) { Text(stringResource(R.string.install_extension)) }
            }
        }
    }
}

@Composable
private fun ExtensionPermissionGroup(titleRes: Int, values: List<String>) {
    if (values.isEmpty()) return
    val colors = LocalNovaColors.current
    Spacer(Modifier.height(14.dp))
    Text(stringResource(titleRes), color = colors.foreground, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
    values.forEach { value ->
        Text("• $value", color = colors.muted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
internal fun ExtensionInstallStatus(
    state: ExtensionInstallState,
    onDismiss: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val message = when (state) {
        ExtensionInstallState.Staging -> stringResource(R.string.extension_staging)
        ExtensionInstallState.Installing -> stringResource(R.string.extension_installing)
        is ExtensionInstallState.Installed -> stringResource(R.string.extension_installed, state.name)
        is ExtensionInstallState.Failed -> extensionFailureMessage(state.reason)
        else -> return
    }
    Surface(
        modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
        color = colors.surfaceSecondary,
        shape = RoundedCornerShape(12.dp),
        border = BorderStroke(1.dp, colors.border),
    ) {
        Row(Modifier.fillMaxWidth().padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(message, modifier = Modifier.weight(1f), color = colors.foreground, fontSize = 13.sp)
            if (state is ExtensionInstallState.Staging || state is ExtensionInstallState.Installing) {
                LinearProgressIndicator(modifier = Modifier.width(72.dp).height(4.dp), color = colors.accent)
            } else {
                TextButton(onClick = onDismiss) { Text(stringResource(R.string.done)) }
            }
        }
    }
}

@Composable
private fun extensionFailureMessage(reason: ExtensionInstallFailure): String = stringResource(
    when (reason) {
        ExtensionInstallFailure.INVALID_FILE_NAME -> R.string.extension_error_invalid_name
        ExtensionInstallFailure.EMPTY_FILE -> R.string.extension_error_empty
        ExtensionInstallFailure.TOO_LARGE -> R.string.extension_error_too_large
        ExtensionInstallFailure.UNAVAILABLE -> R.string.extension_error_unavailable
        ExtensionInstallFailure.BUSY -> R.string.extension_install_busy
        ExtensionInstallFailure.NOT_SIGNED -> R.string.extension_error_not_signed
        ExtensionInstallFailure.CORRUPT_FILE -> R.string.extension_error_corrupt
        ExtensionInstallFailure.INCOMPATIBLE -> R.string.extension_error_incompatible
        ExtensionInstallFailure.BLOCKLISTED -> R.string.extension_error_blocklisted
        ExtensionInstallFailure.SOFT_BLOCKED -> R.string.extension_error_soft_blocked
        ExtensionInstallFailure.NETWORK_FAILURE -> R.string.extension_error_network
        ExtensionInstallFailure.UNSUPPORTED_TYPE -> R.string.extension_error_unsupported
        ExtensionInstallFailure.USER_CANCELLED -> R.string.extension_error_cancelled
        ExtensionInstallFailure.UNKNOWN -> R.string.extension_error_unknown
    },
)

@Composable
private fun ExtensionRow(icon: ImageVector, title: String, subtitle: String, trailing: @Composable () -> Unit) {
    val colors = LocalNovaColors.current
    Row(Modifier.fillMaxWidth().padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(44.dp).background(colors.surfaceSecondary, RoundedCornerShape(12.dp)).border(1.dp, colors.border, RoundedCornerShape(12.dp)),
            contentAlignment = Alignment.Center,
        ) { Icon(icon, null, modifier = Modifier.size(23.dp)) }
        Spacer(Modifier.width(13.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.SemiBold)
            Text(subtitle, color = colors.muted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        trailing()
    }
}

@Composable
private fun RecommendationRow(icon: ImageVector, title: String, rating: String) {
    val colors = LocalNovaColors.current
    Row(Modifier.fillMaxWidth().padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(40.dp).background(colors.surfaceSecondary, RoundedCornerShape(12.dp)).border(1.dp, colors.border, RoundedCornerShape(12.dp)),
            contentAlignment = Alignment.Center,
        ) { Icon(icon, null, modifier = Modifier.size(20.dp)) }
        Spacer(Modifier.width(13.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.SemiBold)
            Text(rating, color = colors.muted, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        }
        Surface(
            modifier = Modifier.height(36.dp).clickable {},
            color = colors.surface,
            shape = RoundedCornerShape(18.dp),
            border = BorderStroke(1.dp, colors.strongBorder),
        ) { Box(Modifier.padding(horizontal = 16.dp), contentAlignment = Alignment.Center) { Text(stringResource(R.string.get), fontSize = 13.sp, fontWeight = FontWeight.SemiBold) } }
    }
}
