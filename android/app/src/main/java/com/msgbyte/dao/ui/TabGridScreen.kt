package com.msgbyte.dao.ui

import android.graphics.Bitmap
import android.net.Uri
import androidx.activity.compose.BackHandler
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
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.composables.icons.lucide.ChevronLeft
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Plus
import com.composables.icons.lucide.X
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.TabThumbnailRepository
import com.msgbyte.dao.ui.theme.LocalNovaColors
import com.msgbyte.dao.ui.theme.daoBrandLogoPainter
import mozilla.components.browser.state.state.TabSessionState

const val TAB_GRID_TEST_TAG = "tab-grid"
const val TAB_CARD_TEST_TAG_PREFIX = "tab-card-"
const val TAB_THUMBNAIL_TEST_TAG_PREFIX = "tab-thumbnail-"

// Page previews are usually light, so the close control keeps a dark scrim in every space.
private val PreviewCloseScrim = Color(0x8C1C1E23)

@Composable
fun TabGridScreen(
    tabs: List<TabSessionState>,
    selectedTabId: String?,
    thumbnailRepository: TabThumbnailRepository,
    onSelect: (String) -> Unit,
    onClose: (String) -> Unit,
    onModeChange: (Boolean) -> Unit,
    onCloseAllPrivateTabs: () -> Unit,
    onNewTab: () -> Unit,
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val privateBrowsing = tabs.firstOrNull { it.id == selectedTabId }?.content?.private == true
    val visibleTabs = tabs.filter { it.content.private == privateBrowsing }
    var thumbnails by remember { mutableStateOf<Map<String, Bitmap>>(emptyMap()) }

    LaunchedEffect(visibleTabs.map { it.id to it.content.private }) {
        thumbnails = visibleTabs.mapNotNull { tab ->
            thumbnailRepository.load(tab.id, tab.content.private)?.let { tab.id to it }
        }.toMap()
    }
    BackHandler(onBack = onBack)

    // New tabs are appended at the end, so open the grid on the selected tab
    // and bring it back on screen whenever the selection or tab count changes.
    val selectedIndex = visibleTabs.indexOfFirst { it.id == selectedTabId }
    val gridState = rememberLazyGridState(initialFirstVisibleItemIndex = selectedIndex.coerceAtLeast(0))
    LaunchedEffect(selectedTabId, visibleTabs.size) {
        if (selectedIndex < 0) return@LaunchedEffect
        val layout = gridState.layoutInfo
        val fullyVisible = layout.visibleItemsInfo.any {
            it.index == selectedIndex &&
                it.offset.y >= layout.viewportStartOffset &&
                it.offset.y + it.size.height <= layout.viewportEndOffset
        }
        if (!fullyVisible) gridState.animateScrollToItem(selectedIndex)
    }

    Surface(
        modifier = Modifier.fillMaxSize().testTag(TAB_GRID_TEST_TAG),
        color = colors.background,
    ) {
        Box(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
            Column(Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier.fillMaxWidth().height(56.dp).padding(start = 4.dp, end = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    IconButton(onClick = onBack) {
                        Icon(Lucide.ChevronLeft, stringResource(R.string.navigate_back))
                    }
                    Spacer(Modifier.weight(1f))
                    if (privateBrowsing) {
                        val closeAllDescription = stringResource(R.string.close_all_private_tabs)
                        TextButton(
                            onClick = onCloseAllPrivateTabs,
                            modifier = Modifier.semantics { contentDescription = closeAllDescription },
                            shape = CircleShape,
                            colors = ButtonDefaults.textButtonColors(
                                containerColor = colors.foreground.copy(alpha = .09f),
                                contentColor = colors.foreground,
                            ),
                            contentPadding = PaddingValues(horizontal = 16.dp),
                        ) {
                            Text(stringResource(R.string.close_all), fontSize = 14.sp, fontWeight = FontWeight.Medium)
                        }
                    }
                }
                Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp)) {
                    Text(
                        stringResource(
                            if (privateBrowsing) R.string.browsing_mode_private else R.string.browsing_mode_regular,
                        ),
                        color = colors.foreground,
                        fontSize = 30.sp,
                        lineHeight = 38.sp,
                        fontWeight = FontWeight.Bold,
                    )
                    Text(
                        stringResource(
                            if (privateBrowsing) R.string.private_tabs_subtitle else R.string.regular_tabs_subtitle,
                        ),
                        color = colors.muted,
                        fontSize = 13.sp,
                    )
                }
                LazyVerticalGrid(
                    columns = GridCells.Fixed(2),
                    modifier = Modifier.weight(1f),
                    state = gridState,
                    // The bottom padding keeps the last row clear of the floating dock.
                    contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 12.dp, bottom = 96.dp),
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    items(visibleTabs, key = { it.id }) { tab ->
                        TabGridCard(
                            tab = tab,
                            selected = tab.id == selectedTabId,
                            selectionColor = if (privateBrowsing) colors.accent else colors.brand,
                            thumbnail = thumbnails[tab.id],
                            onSelect = { onSelect(tab.id) },
                            onClose = { onClose(tab.id) },
                        )
                    }
                }
            }
            TabGridDock(
                regularCount = tabs.count { !it.content.private },
                privateCount = tabs.count { it.content.private },
                privateBrowsing = privateBrowsing,
                onModeChange = onModeChange,
                onNewTab = onNewTab,
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 20.dp),
            )
        }
    }
}

@Composable
private fun TabGridDock(
    regularCount: Int,
    privateCount: Int,
    privateBrowsing: Boolean,
    onModeChange: (Boolean) -> Unit,
    onNewTab: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .shadow(12.dp, CircleShape)
                .background(colors.surface, CircleShape)
                .padding(4.dp)
                .selectableGroup(),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            DockModeButton(
                label = stringResource(R.string.regular_tabs, regularCount),
                selected = !privateBrowsing,
                onClick = { onModeChange(false) },
            )
            DockModeButton(
                label = stringResource(R.string.private_tabs, privateCount),
                selected = privateBrowsing,
                onClick = { onModeChange(true) },
            )
        }
        Surface(
            onClick = onNewTab,
            modifier = Modifier.size(52.dp),
            shape = CircleShape,
            color = colors.accent,
            contentColor = colors.onAccent,
            shadowElevation = 6.dp,
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    Lucide.Plus,
                    stringResource(if (privateBrowsing) R.string.new_private_tab else R.string.new_tab),
                    modifier = Modifier.size(22.dp),
                )
            }
        }
    }
}

@Composable
private fun DockModeButton(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    val colors = LocalNovaColors.current
    Box(
        modifier = Modifier
            .height(44.dp)
            .clip(CircleShape)
            .background(if (selected) colors.strongBorder else Color.Transparent)
            .selectable(selected = selected, role = Role.RadioButton, onClick = onClick)
            .padding(horizontal = 18.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label,
            color = if (selected) colors.foreground else colors.muted,
            fontSize = 14.sp,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TabGridCard(
    tab: TabSessionState,
    selected: Boolean,
    selectionColor: Color,
    thumbnail: Bitmap?,
    onSelect: () -> Unit,
    onClose: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val host = remember(tab.content.url) {
        Uri.parse(tab.content.url).host ?: tab.content.url
    }
    val cardShape = RoundedCornerShape(18.dp)
    val previewShape = RoundedCornerShape(14.dp)
    val dismissState = rememberSwipeToDismissBoxState(
        confirmValueChange = { value ->
            if (value == SwipeToDismissBoxValue.StartToEnd) {
                onClose()
                true
            } else {
                false
            }
        },
        positionalThreshold = { distance -> distance * 0.35f },
    )
    SwipeToDismissBox(
        state = dismissState,
        enableDismissFromStartToEnd = true,
        enableDismissFromEndToStart = false,
        backgroundContent = {
            // Only drawn mid-swipe, so its antialiased corners never outline the card at rest.
            if (dismissState.dismissDirection == SwipeToDismissBoxValue.StartToEnd) {
                Box(
                    Modifier.fillMaxSize().background(colors.accent, cardShape),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    Icon(
                        Lucide.X,
                        contentDescription = null,
                        tint = colors.onAccent,
                        modifier = Modifier.padding(start = 24.dp),
                    )
                }
            }
        },
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(cardShape)
                // Opaque so the swipe-to-close background stays behind the card while it slides.
                .background(colors.background)
                .testTag("$TAB_CARD_TEST_TAG_PREFIX${tab.id}")
                .semantics { this.selected = selected }
                .clickable(onClick = onSelect),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(208.dp)
                    .border(2.dp, if (selected) selectionColor else Color.Transparent, cardShape)
                    .padding(4.dp)
                    .clip(previewShape)
                    .background(colors.surfaceSecondary)
                    .border(1.dp, colors.border, previewShape),
                contentAlignment = Alignment.Center,
            ) {
                if (thumbnail != null) {
                    Image(
                        bitmap = thumbnail.asImageBitmap(),
                        contentDescription = null,
                        modifier = Modifier.fillMaxSize()
                            .testTag("$TAB_THUMBNAIL_TEST_TAG_PREFIX${tab.id}"),
                        contentScale = ContentScale.Crop,
                        alignment = Alignment.TopCenter,
                    )
                } else {
                    Image(daoBrandLogoPainter(), null, Modifier.size(36.dp).alpha(.28f))
                }
                IconButton(
                    onClick = onClose,
                    modifier = Modifier.align(Alignment.TopEnd).size(40.dp),
                ) {
                    Box(
                        Modifier.size(26.dp).background(PreviewCloseScrim, CircleShape),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            Lucide.X,
                            stringResource(R.string.close_tab),
                            tint = Color.White,
                            modifier = Modifier.size(14.dp),
                        )
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth().padding(start = 8.dp, end = 8.dp, top = 6.dp, bottom = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                tab.content.icon?.let { icon ->
                    Image(
                        bitmap = icon.asImageBitmap(),
                        contentDescription = null,
                        modifier = Modifier.size(16.dp).clip(RoundedCornerShape(4.dp)),
                    )
                    Spacer(Modifier.width(6.dp))
                }
                Text(
                    tab.content.title.ifBlank {
                        if (tab.content.url == "about:blank") stringResource(R.string.new_tab)
                        else host
                    },
                    color = colors.foreground,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Medium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}
