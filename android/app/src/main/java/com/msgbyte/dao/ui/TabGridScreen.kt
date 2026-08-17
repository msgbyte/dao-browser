package com.msgbyte.dao.ui

import android.graphics.Bitmap
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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.SwipeToDismissBox
import androidx.compose.material3.SwipeToDismissBoxValue
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSwipeToDismissBoxState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
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
import mozilla.components.browser.state.state.TabSessionState

const val TAB_GRID_TEST_TAG = "tab-grid"
const val TAB_CARD_TEST_TAG_PREFIX = "tab-card-"
const val TAB_THUMBNAIL_TEST_TAG_PREFIX = "tab-thumbnail-"

@Composable
fun TabGridScreen(
    tabs: List<TabSessionState>,
    selectedTabId: String?,
    thumbnailRepository: TabThumbnailRepository,
    onSelect: (String) -> Unit,
    onClose: (String) -> Unit,
    onNewTab: () -> Unit,
    onBack: () -> Unit,
) {
    val colors = LocalNovaColors.current
    var thumbnails by remember { mutableStateOf<Map<String, Bitmap>>(emptyMap()) }

    LaunchedEffect(tabs.map { it.id to it.content.private }) {
        thumbnails = tabs.mapNotNull { tab ->
            thumbnailRepository.load(tab.id, tab.content.private)?.let { tab.id to it }
        }.toMap()
    }
    BackHandler(onBack = onBack)

    Surface(
        modifier = Modifier.fillMaxSize().testTag(TAB_GRID_TEST_TAG),
        color = colors.background,
    ) {
        Column(Modifier.fillMaxSize().windowInsetsPadding(androidx.compose.foundation.layout.WindowInsets.safeDrawing)) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                IconButton(onClick = onBack) {
                    Icon(Lucide.ChevronLeft, stringResource(R.string.navigate_back))
                }
                Text(
                    stringResource(R.string.tabs_title, tabs.size),
                    modifier = Modifier.weight(1f),
                    color = colors.foreground,
                    fontSize = 20.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                IconButton(onClick = onNewTab) {
                    Icon(Lucide.Plus, stringResource(R.string.new_tab))
                }
            }
            LazyVerticalGrid(
                columns = GridCells.Fixed(2),
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(start = 12.dp, end = 12.dp, bottom = 20.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                items(tabs, key = { it.id }) { tab ->
                    TabGridCard(
                        tab = tab,
                        selected = tab.id == selectedTabId,
                        thumbnail = thumbnails[tab.id],
                        onSelect = { onSelect(tab.id) },
                        onClose = { onClose(tab.id) },
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TabGridCard(
    tab: TabSessionState,
    selected: Boolean,
    thumbnail: Bitmap?,
    onSelect: () -> Unit,
    onClose: () -> Unit,
) {
    val colors = LocalNovaColors.current
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
            Box(
                Modifier.fillMaxSize().background(colors.accent, RoundedCornerShape(14.dp)),
                contentAlignment = Alignment.CenterStart,
            ) {
                Icon(
                    Lucide.X,
                    contentDescription = null,
                    tint = colors.onAccent,
                    modifier = Modifier.padding(start = 24.dp),
                )
            }
        },
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .height(252.dp)
                .testTag("$TAB_CARD_TEST_TAG_PREFIX${tab.id}")
                .semantics { this.selected = selected }
                .clip(RoundedCornerShape(14.dp))
                .clickable(onClick = onSelect)
                .then(
                    if (selected) Modifier.border(2.dp, colors.accent, RoundedCornerShape(14.dp))
                    else Modifier.border(1.dp, colors.border, RoundedCornerShape(14.dp)),
                ),
            shape = RoundedCornerShape(14.dp),
            color = colors.surface,
        ) {
            Column {
                Box(
                    modifier = Modifier.fillMaxWidth().weight(1f)
                        .background(colors.surfaceSecondary),
                    contentAlignment = Alignment.Center,
                ) {
                    if (thumbnail != null) {
                        Image(
                            bitmap = thumbnail.asImageBitmap(),
                            contentDescription = null,
                            modifier = Modifier.fillMaxSize()
                                .testTag("$TAB_THUMBNAIL_TEST_TAG_PREFIX${tab.id}"),
                            contentScale = ContentScale.Crop,
                        )
                    } else {
                        Text("Dao", color = colors.faint, fontWeight = FontWeight.SemiBold)
                    }
                    IconButton(
                        onClick = onClose,
                        modifier = Modifier.align(Alignment.TopEnd).padding(4.dp).size(34.dp)
                            .background(colors.surface.copy(alpha = .9f), RoundedCornerShape(10.dp)),
                    ) {
                        Icon(
                            Lucide.X,
                            stringResource(R.string.close_tab),
                            tint = colors.foreground,
                            modifier = Modifier.size(17.dp),
                        )
                    }
                }
                Column(Modifier.fillMaxWidth().padding(horizontal = 10.dp, vertical = 9.dp)) {
                    Text(
                        tab.content.title.ifBlank {
                            if (tab.content.url == "about:blank") stringResource(R.string.new_tab)
                            else tab.content.url
                        },
                        color = colors.foreground,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        tab.content.url,
                        color = colors.muted,
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}
