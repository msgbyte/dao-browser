package com.msgbyte.dao.ui

import android.content.Intent
import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import android.util.Log
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.indication
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.ripple
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.composables.icons.lucide.Bookmark
import com.composables.icons.lucide.ChevronLeft
import com.composables.icons.lucide.ChevronRight
import com.composables.icons.lucide.ChevronDown
import com.composables.icons.lucide.ChevronUp
import com.composables.icons.lucide.Download
import com.composables.icons.lucide.Ellipsis
import com.composables.icons.lucide.History
import com.composables.icons.lucide.Lock
import com.composables.icons.lucide.Globe
import com.composables.icons.lucide.House
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Moon
import com.composables.icons.lucide.Puzzle
import com.composables.icons.lucide.RefreshCw
import com.composables.icons.lucide.ScanLine
import com.composables.icons.lucide.Search
import com.composables.icons.lucide.SearchCode
import com.composables.icons.lucide.Settings
import com.composables.icons.lucide.Share
import com.composables.icons.lucide.Star
import com.composables.icons.lucide.Sun
import com.composables.icons.lucide.X
import com.msgbyte.dao.R
import com.msgbyte.dao.about.readAboutAppInfo
import com.msgbyte.dao.browser.AppConfiguration
import com.msgbyte.dao.browser.AmoStoreViewModel
import com.msgbyte.dao.browser.BrowserTabsController
import com.msgbyte.dao.browser.BrowserLibraryRepository
import com.msgbyte.dao.browser.BookmarkEntry
import com.msgbyte.dao.browser.ExtensionInstallState
import com.msgbyte.dao.browser.HistoryVisit
import com.msgbyte.dao.browser.NavigationTargetResolver
import com.msgbyte.dao.browser.SiteSecurityDetails
import com.msgbyte.dao.browser.SiteSecurityState
import com.msgbyte.dao.browser.SystemDownloadRepository
import com.msgbyte.dao.browser.TabThumbnailRepository
import com.msgbyte.dao.ui.theme.LocalNovaColors
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.launch
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.browser.state.state.TabSessionState
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.core.content.ContextCompat

const val BROWSER_SURFACE_TEST_TAG = "browser-surface"
const val BROWSER_CONTROLS_TEST_TAG = "browser-controls"
const val NEW_TAB_SCREEN_TEST_TAG = "new-tab-screen"
const val EDGE_BACK_GESTURE_TEST_TAG = "edge-back-gesture"
internal const val SECURITY_RIPPLE_TEST_TAG = "security-ripple"
internal const val BOOKMARK_DRAWER_TILE_TEST_TAG = "bookmark-drawer-tile"
internal const val BOOKMARK_DRAWER_ICON_TEST_TAG = "bookmark-drawer-icon"

private val NovaEasing = CubicBezierEasing(0.22f, 0.61f, 0.36f, 1f)
private const val BrowserTransitionDurationMillis = 220
private const val BrowserTransitionOffsetDivisor = 10
private const val TabGridInitialScale = 0.96f
private val EdgeBackGestureWidth = 24.dp
private val EdgeBackGestureThreshold = 64.dp

private fun <T> browserMotionSpec() = tween<T>(
    durationMillis = BrowserTransitionDurationMillis,
    easing = NovaEasing,
)

private fun browserContentTransform(
    transition: BrowserScreenTransition,
): ContentTransform = when (transition) {
    BrowserScreenTransition.Forward ->
        (slideInHorizontally(browserMotionSpec()) {
            it / BrowserTransitionOffsetDivisor
        } + fadeIn(browserMotionSpec())).togetherWith(
            slideOutHorizontally(browserMotionSpec()) {
                -it / BrowserTransitionOffsetDivisor
            } + fadeOut(browserMotionSpec()),
        )
    BrowserScreenTransition.Backward ->
        (slideInHorizontally(browserMotionSpec()) {
            -it / BrowserTransitionOffsetDivisor
        } + fadeIn(browserMotionSpec())).togetherWith(
            slideOutHorizontally(browserMotionSpec()) {
                it / BrowserTransitionOffsetDivisor
            } + fadeOut(browserMotionSpec()),
        )
    BrowserScreenTransition.OpenTabGrid ->
        (fadeIn(browserMotionSpec()) +
            scaleIn(browserMotionSpec(), initialScale = TabGridInitialScale)).togetherWith(
            fadeOut(browserMotionSpec()),
        )
    BrowserScreenTransition.CloseTabGrid ->
        fadeIn(browserMotionSpec()).togetherWith(
            fadeOut(browserMotionSpec()) +
                scaleOut(browserMotionSpec(), targetScale = TabGridInitialScale),
        )
    BrowserScreenTransition.Immediate ->
        EnterTransition.None.togetherWith(ExitTransition.None)
}

private val FilledStarIcon = ImageVector.Builder(
    name = "FilledStar",
    defaultWidth = 24.dp,
    defaultHeight = 24.dp,
    viewportWidth = 24f,
    viewportHeight = 24f,
).apply {
    addPath(
        pathData = PathParser().parsePathString(
            "M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 " +
                "1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 " +
                "2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 " +
                "2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 " +
                "2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 " +
                "2.122 0 0 0 1.597-1.16z",
        ).toNodes(),
        fill = SolidColor(Color.Black),
        stroke = SolidColor(Color.Black),
        strokeLineWidth = 2f,
        strokeLineCap = StrokeCap.Round,
        strokeLineJoin = StrokeJoin.Round,
    )
}.build()

enum class BrowserDestination {
    NewTab,
    AddressEdit,
    Browsing,
    Settings,
    About,
    OpenSourceLicenses,
    History,
    Bookmarks,
    Downloads,
    Extensions,
    AmoStore,
    Tabs,
}

@Composable
fun BrowserScreen(
    engine: Engine,
    controller: BrowserTabsController,
    thumbnailRepository: TabThumbnailRepository,
    library: BrowserLibraryRepository,
    downloads: SystemDownloadRepository,
    extensions: com.msgbyte.dao.browser.ExtensionRepository,
    amoStoreViewModel: AmoStoreViewModel,
    resolver: NavigationTargetResolver,
    preferences: com.msgbyte.dao.browser.BrowserPreferenceState,
    onDarkThemeChange: (Boolean) -> Unit,
    onFontScaleChange: (com.msgbyte.dao.browser.BrowserFontScale) -> Unit,
    onSearchEngineChange: (com.msgbyte.dao.browser.BrowserSearchEngine) -> Unit,
    onTrackingProtectionChange: (Boolean) -> Unit,
    onDefaultPrivateBrowsingChange: (Boolean) -> Unit,
    onRemoteDebuggingChange: (Boolean) -> Unit,
    onEnableRemoteDebuggingWithAcknowledgement: () -> Unit,
) {
    val browserState by controller.state.collectAsStateWithLifecycle()
    val selectedTab = browserState.tabs.firstOrNull { it.id == browserState.selectedTabId }
    val thumbnailCapture = remember { BrowserThumbnailCapture() }
    var animatedDestination by remember {
        mutableStateOf(
            BrowserAnimatedDestination(
                destination = BrowserDestination.NewTab,
                transition = BrowserScreenTransition.Immediate,
            ),
        )
    }
    val destination = animatedDestination.destination
    val navigateTo: (BrowserDestination, BrowserNavigationDirection) -> Unit =
        { target, direction ->
            animatedDestination = BrowserAnimatedDestination(
                destination = target,
                transition = resolveBrowserScreenTransition(
                    from = animatedDestination.destination,
                    to = target,
                    direction = direction,
                ),
            )
        }
    var returnDestination by remember { mutableStateOf(BrowserDestination.Browsing) }
    var addressEditUrl by remember { mutableStateOf("") }
    var homeReturnTabId by remember { mutableStateOf<String?>(null) }
    var homeReturnUrl by remember { mutableStateOf<String?>(null) }
    var extensionPopupSession by remember { mutableStateOf<EngineSession?>(null) }
    var amoTransientInstallGuid by rememberSaveable { mutableStateOf<String?>(null) }
    val installedExtensions by extensions.extensions.collectAsStateWithLifecycle()
    val extensionInstallState by extensions.installState.collectAsStateWithLifecycle()
    val history by library.history.collectAsStateWithLifecycle()
    val bookmarks by library.bookmarks.collectAsStateWithLifecycle()
    val folders by library.folders.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val rootView = LocalView.current
    val thumbnailCaptures = remember(scope, thumbnailCapture, thumbnailRepository) {
        TabThumbnailCaptureCoordinator(
            scope = scope,
            captureThumbnail = thumbnailCapture::capture,
            saveThumbnail = thumbnailRepository::save,
            deleteThumbnail = thumbnailRepository::delete,
            onError = { error ->
                Log.e("BrowserScreen", "Unable to manage tab thumbnail", error)
            },
        )
    }
    var initialTabApplied by remember { mutableStateOf(false) }

    AmoInstallTrackingEffect(
        transientInstallGuid = amoTransientInstallGuid,
        installedExtensionGuids = installedExtensions.mapTo(mutableSetOf()) { it.id },
        installState = extensionInstallState,
        onTransientInstallGuidChange = { amoTransientInstallGuid = it },
    )

    DisposableEffect(extensions, controller) {
        extensions.setPopupSessionHandler { popupSession ->
            extensionPopupSession?.close()
            extensionPopupSession = popupSession
            navigateTo(BrowserDestination.Browsing, BrowserNavigationDirection.Immediate)
        }
        onDispose {
            extensions.setPopupSessionHandler(null)
            extensionPopupSession?.close()
        }
    }

    BackHandler(enabled = extensionPopupSession != null) {
        extensionPopupSession?.close()
        extensionPopupSession = null
    }

    LaunchedEffect(selectedTab?.id, selectedTab?.engineState?.engineSession) {
        selectedTab
            ?.takeIf { it.engineState.engineSession == null }
            ?.let { controller.ensureSession(it.id) }
    }

    LaunchedEffect(selectedTab?.id) {
        if (!initialTabApplied && selectedTab != null) {
            navigateTo(
                if (selectedTab.content.url == AppConfiguration.INITIAL_URL) {
                    BrowserDestination.NewTab
                } else {
                    BrowserDestination.Browsing
                },
                BrowserNavigationDirection.Immediate,
            )
            initialTabApplied = true
        }
    }

    BackHandler(
        enabled = destination == BrowserDestination.NewTab && selectedTab?.id == homeReturnTabId,
    ) {
        if (selectedTab?.content?.canGoBack == true) {
            controller.goBack()
        } else {
            homeReturnUrl?.let(controller::navigate)
        }
        homeReturnTabId = null
        homeReturnUrl = null
        navigateTo(BrowserDestination.Browsing, BrowserNavigationDirection.Immediate)
    }

    val captureSelected = {
        controller.selectedTab()?.let { tab ->
            thumbnailCaptures.capture(tab.id, tab.content.private)
        }
    }
    val openTabsFromComposePage: () -> Unit = {
        val tab = controller.selectedTab()
        val bitmap = try {
            captureViewThumbnail(rootView)
        } catch (error: Throwable) {
            Log.e("BrowserScreen", "Unable to capture Compose tab thumbnail", error)
            null
        }
        if (tab == null || bitmap == null) {
            navigateTo(BrowserDestination.Tabs, BrowserNavigationDirection.Immediate)
        } else {
            scope.launch(start = CoroutineStart.UNDISPATCHED) {
                thumbnailCaptures.captureAndWait(tab.id, tab.content.private) { bitmap }
                navigateTo(BrowserDestination.Tabs, BrowserNavigationDirection.Immediate)
            }
            Unit
        }
    }

    val navigate: (String) -> Unit = { rawTarget ->
        resolver.resolve(rawTarget)?.let { target ->
            if (controller.selectedTab() == null) controller.createTab()
            homeReturnTabId = null
            homeReturnUrl = null
            controller.navigate(target)
            navigateTo(BrowserDestination.Browsing, BrowserNavigationDirection.Immediate)
        }
    }
    val openUtility: (BrowserDestination) -> Unit = { target ->
        returnDestination = destination
        captureSelected()
        navigateTo(target, BrowserNavigationDirection.Forward)
    }
    val closeUtility = {
        navigateTo(
            if (returnDestination == BrowserDestination.NewTab) {
                BrowserDestination.NewTab
            } else {
                BrowserDestination.Browsing
            },
            BrowserNavigationDirection.Backward,
        )
    }

    BackHandler(enabled = destination == BrowserDestination.Settings) {
        closeUtility()
    }

    BackHandler(enabled = destination == BrowserDestination.About) {
        navigateTo(BrowserDestination.Settings, BrowserNavigationDirection.Backward)
    }

    BackHandler(enabled = destination == BrowserDestination.OpenSourceLicenses) {
        navigateTo(BrowserDestination.About, BrowserNavigationDirection.Backward)
    }

    BackHandler(enabled = destination == BrowserDestination.AmoStore) {
        navigateTo(BrowserDestination.Extensions, BrowserNavigationDirection.Backward)
    }

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = LocalNovaColors.current.background,
    ) {
        val popupSession = extensionPopupSession
        if (popupSession != null) {
            ExtensionPopupScreen(
                engine = engine,
                session = popupSession,
                onClose = {
                    popupSession.close()
                    extensionPopupSession = null
                },
            )
        } else {
            AnimatedContent(
                targetState = animatedDestination,
                transitionSpec = { browserContentTransform(targetState.transition) },
                label = "browserDestination",
            ) { targetState ->
                when (targetState.destination) {
                    BrowserDestination.NewTab -> NewTabScreen(
                        bookmarks = bookmarks,
                        history = history,
                        tabCount = browserState.tabs.size,
                        onTabs = openTabsFromComposePage,
                        onSettings = { openUtility(BrowserDestination.Settings) },
                        onNavigate = navigate,
                    )
                    BrowserDestination.AddressEdit -> NewTabScreen(
                        bookmarks = bookmarks,
                        history = history,
                        initialQuery = addressEditUrl,
                        startExpanded = true,
                        tabCount = browserState.tabs.size,
                        onTabs = openTabsFromComposePage,
                        onSettings = { openUtility(BrowserDestination.Settings) },
                        onExitExpanded = {
                            navigateTo(BrowserDestination.Browsing, BrowserNavigationDirection.Immediate)
                        },
                        onNavigate = navigate,
                    )
                    BrowserDestination.Browsing -> BrowsingScreen(
                        engine = engine,
                        controller = controller,
                        tab = selectedTab,
                        tabCount = browserState.tabs.size,
                        thumbnailCapture = thumbnailCapture,
                        library = library,
                        darkTheme = preferences.darkTheme,
                        onDarkThemeChange = onDarkThemeChange,
                        onEditAddress = { url ->
                            addressEditUrl = url
                            navigateTo(
                                BrowserDestination.AddressEdit,
                                BrowserNavigationDirection.Immediate,
                            )
                        },
                        onOpenTabs = {
                            captureSelected()
                            navigateTo(BrowserDestination.Tabs, BrowserNavigationDirection.Immediate)
                        },
                        onHome = {
                            homeReturnUrl = controller.selectedTab()?.content?.url
                            controller.goHome()
                            homeReturnTabId = controller.selectedTab()?.id
                            navigateTo(BrowserDestination.NewTab, BrowserNavigationDirection.Immediate)
                        },
                        onNavigate = navigate,
                        onOpen = openUtility,
                    )
                    BrowserDestination.Settings -> SettingsScreen(
                        preferences = preferences,
                        onDarkThemeChange = onDarkThemeChange,
                        onFontScaleChange = onFontScaleChange,
                        onSearchEngineChange = onSearchEngineChange,
                        onTrackingProtectionChange = onTrackingProtectionChange,
                        onDefaultPrivateBrowsingChange = onDefaultPrivateBrowsingChange,
                        onRemoteDebuggingChange = onRemoteDebuggingChange,
                        onEnableRemoteDebuggingWithAcknowledgement =
                            onEnableRemoteDebuggingWithAcknowledgement,
                        onOpenAbout = {
                            navigateTo(BrowserDestination.About, BrowserNavigationDirection.Forward)
                        },
                        onBack = closeUtility,
                    )
                    BrowserDestination.About -> AboutScreen(
                        appInfo = readAboutAppInfo(LocalContext.current),
                        onOpenLicenses = {
                            navigateTo(
                                BrowserDestination.OpenSourceLicenses,
                                BrowserNavigationDirection.Forward,
                            )
                        },
                        onBack = {
                            navigateTo(BrowserDestination.Settings, BrowserNavigationDirection.Backward)
                        },
                    )
                    BrowserDestination.OpenSourceLicenses -> OpenSourceLicensesScreen(
                        onBack = {
                            navigateTo(BrowserDestination.About, BrowserNavigationDirection.Backward)
                        },
                    )
                    BrowserDestination.History -> HistoryScreen(
                        visits = history,
                        onClear = { scope.launch { library.clearHistory() } },
                        onDelete = { id -> scope.launch { library.deleteHistoryVisit(id) } },
                        onBack = closeUtility,
                        onNavigate = navigate,
                    )
                    BrowserDestination.Bookmarks -> BookmarksScreen(
                        bookmarks = bookmarks,
                        folders = folders,
                        onCreateFolder = { title -> scope.launch { library.createFolder(title) } },
                        onUpdateBookmark = { id, title, folderId, kind ->
                            scope.launch { library.updateBookmark(id, title, folderId, kind) }
                        },
                        onDeleteBookmark = { id -> scope.launch { library.deleteBookmark(id) } },
                        onBack = closeUtility,
                        onNavigate = navigate,
                    )
                    BrowserDestination.Downloads -> DownloadsScreen(repository = downloads, onBack = closeUtility)
                    BrowserDestination.Extensions -> ExtensionsScreen(
                        repository = extensions,
                        onOpenStore = {
                            navigateTo(BrowserDestination.AmoStore, BrowserNavigationDirection.Forward)
                        },
                        onActionInvoked = {
                            navigateTo(BrowserDestination.Browsing, BrowserNavigationDirection.Immediate)
                        },
                        onBack = closeUtility,
                    )
                    BrowserDestination.AmoStore -> {
                        val storeState by amoStoreViewModel.state.collectAsStateWithLifecycle()
                        AmoStoreScreen(
                            state = storeState,
                            installedExtensionGuids = installedExtensions.mapTo(mutableSetOf()) { it.id },
                            installState = extensionInstallState,
                            transientInstallGuid = amoTransientInstallGuid,
                            onTransientInstallGuidChange = { amoTransientInstallGuid = it },
                            onSearch = amoStoreViewModel::search,
                            onRetry = amoStoreViewModel::retry,
                            onLoadNext = amoStoreViewModel::loadNext,
                            onInstall = { addon -> extensions.installFromAmo(addon.installUrl) },
                            onDismissInstallResult = extensions::dismissResult,
                            onResolvePermission = extensions::resolvePermission,
                            onBack = {
                                navigateTo(
                                    BrowserDestination.Extensions,
                                    BrowserNavigationDirection.Backward,
                                )
                            },
                        )
                    }
                    BrowserDestination.Tabs -> TabGridScreen(
                        tabs = browserState.tabs,
                        selectedTabId = browserState.selectedTabId,
                        thumbnailRepository = thumbnailRepository,
                        onSelect = { tabId ->
                            controller.selectTab(tabId)
                            navigateTo(
                                if (controller.selectedTab()?.content?.url == AppConfiguration.INITIAL_URL) {
                                    BrowserDestination.NewTab
                                } else {
                                    BrowserDestination.Browsing
                                },
                                BrowserNavigationDirection.Immediate,
                            )
                        },
                        onClose = { tabId ->
                            scope.launch(start = CoroutineStart.UNDISPATCHED) {
                                thumbnailCaptures.close(tabId)
                            }
                            controller.closeTab(tabId)
                        },
                        onNewTab = {
                            controller.createTab()
                            navigateTo(BrowserDestination.NewTab, BrowserNavigationDirection.Immediate)
                        },
                        onBack = {
                            navigateTo(
                                if (controller.selectedTab()?.content?.url == AppConfiguration.INITIAL_URL) {
                                    BrowserDestination.NewTab
                                } else {
                                    BrowserDestination.Browsing
                                },
                                BrowserNavigationDirection.Immediate,
                            )
                        },
                    )
                }
            }
        }
    }
}

@Composable
internal fun AmoInstallTrackingEffect(
    transientInstallGuid: String?,
    installedExtensionGuids: Set<String>,
    installState: ExtensionInstallState,
    onTransientInstallGuidChange: (String?) -> Unit,
) {
    LaunchedEffect(transientInstallGuid, installedExtensionGuids, installState) {
        if (transientInstallGuid != null &&
            (transientInstallGuid in installedExtensionGuids ||
                installState is ExtensionInstallState.Failed)
        ) {
            onTransientInstallGuidChange(null)
        }
    }
}

@Composable
private fun ExtensionPopupScreen(
    engine: Engine,
    session: EngineSession,
    onClose: () -> Unit,
) {
    val colors = LocalNovaColors.current
    Column(
        Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .background(colors.background),
    ) {
        ScreenHeader(stringResource(R.string.extensions), onClose)
        BrowserSurface(
            engine = engine,
            session = session,
            modifier = Modifier.fillMaxWidth().weight(1f),
        )
    }
}

@Composable
private fun NewTabScreen(
    bookmarks: List<BookmarkEntry>,
    history: List<HistoryVisit>,
    tabCount: Int,
    onTabs: () -> Unit,
    onSettings: () -> Unit,
    initialQuery: String = "",
    startExpanded: Boolean = false,
    onExitExpanded: (() -> Unit)? = null,
    onNavigate: (String) -> Unit,
) {
    val colors = LocalNovaColors.current
    val focusRequester = remember { FocusRequester() }
    var searchExpanded by remember(startExpanded) { mutableStateOf(startExpanded) }
    var scannerOpen by remember { mutableStateOf(false) }
    var query by remember(initialQuery) { mutableStateOf(initialQuery) }
    val now = remember { LocalDateTime.now() }
    val datePattern = stringResource(R.string.new_tab_date_format)
    val topOffset by animateDpAsState(
        targetValue = if (searchExpanded) 16.dp else 420.dp,
        animationSpec = tween(330, easing = NovaEasing),
        label = "searchOffset",
    )
    val results = remember(query, bookmarks, history) {
        buildLibrarySuggestions(query, bookmarks, history)
    }

    LaunchedEffect(searchExpanded) {
        if (searchExpanded) focusRequester.requestFocus()
    }

    BackHandler(enabled = scannerOpen || searchExpanded) {
        if (scannerOpen) scannerOpen = false else if (onExitExpanded != null) {
            onExitExpanded()
        } else {
            searchExpanded = false
            query = ""
        }
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .testTag(NEW_TAB_SCREEN_TEST_TAG),
    ) {
        AnimatedVisibility(
            visible = !searchExpanded,
            modifier = Modifier.align(Alignment.TopEnd).padding(12.dp),
            enter = fadeIn(tween(180)),
            exit = fadeOut(tween(140)),
        ) {
            IconButton(onClick = onSettings) {
                Icon(
                    Lucide.Settings,
                    contentDescription = stringResource(R.string.settings),
                    tint = colors.muted,
                    modifier = Modifier.size(20.dp),
                )
            }
        }

        AnimatedVisibility(
            visible = !searchExpanded,
            modifier = Modifier.align(Alignment.Center).offset(y = (-150).dp),
            exit = fadeOut(tween(280)) + slideOutVertically { -14 },
            enter = fadeIn(tween(280)) + slideInVertically { -14 },
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Image(
                    painter = painterResource(R.drawable.dao_brand_logo),
                    contentDescription = stringResource(R.string.dao_logo_content_description),
                    modifier = Modifier
                        .size(64.dp)
                        .padding(3.dp),
                )
                Spacer(Modifier.height(22.dp))
                Text(
                    formatNewTabDate(now.toLocalDate(), datePattern),
                    color = colors.muted,
                    fontSize = 11.sp,
                    letterSpacing = 1.5.sp,
                    fontFamily = FontFamily.Monospace,
                )
                Spacer(Modifier.height(9.dp))
                Text(
                    stringResource(
                        when (greetingPeriod(now.hour)) {
                            GreetingPeriod.MORNING -> R.string.new_tab_greeting_morning
                            GreetingPeriod.AFTERNOON -> R.string.new_tab_greeting_afternoon
                            GreetingPeriod.EVENING -> R.string.new_tab_greeting_evening
                        },
                    ),
                    color = colors.foreground,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    stringResource(R.string.new_tab_question),
                    color = colors.muted,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.SemiBold,
                )
            }
        }

        SearchField(
            value = query,
            expanded = searchExpanded,
            onValueChange = { query = it },
            onActivate = {
                searchExpanded = true
            },
            onTrailingClick = {
                if (searchExpanded) {
                    if (query.isNotBlank()) {
                        query = ""
                    } else if (onExitExpanded != null) {
                        onExitExpanded()
                    } else {
                        searchExpanded = false
                    }
                } else {
                    scannerOpen = true
                }
            },
            onSubmit = { if (query.isNotBlank()) onNavigate(query) },
            tabCount = tabCount,
            onTabs = onTabs,
            focusRequester = focusRequester,
            modifier = Modifier
                .align(Alignment.TopCenter)
                .offset(y = topOffset)
                .padding(horizontal = 20.dp),
        )

        AnimatedVisibility(
            visible = searchExpanded,
            modifier = Modifier.padding(top = 88.dp),
            enter = fadeIn(tween(260, delayMillis = 40)) + slideInVertically { 8 },
            exit = fadeOut(tween(160)),
        ) {
            SuggestionList(
                query = query,
                results = results,
                onNavigate = { suggestion ->
                    onNavigate(if (suggestion.type == SuggestionType.Search) suggestion.title else suggestion.target)
                },
            )
        }

        if (scannerOpen) {
            ScannerOverlay(
                onClose = { scannerOpen = false },
                onResult = { content ->
                    scannerOpen = false
                    onNavigate(content)
                },
            )
        }
    }
}

@Composable
private fun SearchField(
    value: String,
    expanded: Boolean,
    onValueChange: (String) -> Unit,
    onActivate: () -> Unit,
    onTrailingClick: () -> Unit,
    onSubmit: () -> Unit,
    tabCount: Int,
    onTabs: () -> Unit,
    focusRequester: FocusRequester,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    val tabSwitcherDescription = stringResource(R.string.tab_switcher)
    val borderColor = if (expanded) colors.accent else colors.strongBorder
    val inputTextStyle = androidx.compose.ui.text.TextStyle(
        color = colors.foreground,
        fontSize = 16.sp,
        lineHeight = 16.sp,
    )
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .clip(CircleShape)
            .background(colors.surface, CircleShape)
            .border(1.dp, borderColor, CircleShape)
            .clickable { onActivate() }
            .padding(start = 18.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Lucide.Search, contentDescription = null, tint = colors.muted, modifier = Modifier.size(19.dp))
        Spacer(Modifier.width(10.dp))
        if (expanded) {
            BasicTextField(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier
                    .weight(1f)
                    .focusRequester(focusRequester),
                singleLine = true,
                textStyle = inputTextStyle,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Go),
                keyboardActions = KeyboardActions(onGo = { onSubmit() }),
                decorationBox = { inner ->
                    Box(
                        modifier = Modifier.fillMaxWidth(),
                        contentAlignment = Alignment.CenterStart,
                    ) {
                        if (value.isEmpty()) {
                            Text(
                                stringResource(R.string.address_hint),
                                style = inputTextStyle.copy(color = colors.faint),
                            )
                        }
                        inner()
                    }
                },
            )
        } else {
            Text(
                text = stringResource(R.string.address_hint),
                modifier = Modifier.weight(1f),
                color = colors.faint,
                fontSize = 16.sp,
            )
        }
        if (!expanded) {
            TabCountButton(
                tabCount = tabCount,
                contentDescription = tabSwitcherDescription,
                onClick = onTabs,
                modifier = Modifier.size(36.dp),
            )
            Box(Modifier.width(1.dp).height(24.dp).background(colors.strongBorder))
            Spacer(Modifier.width(4.dp))
        }
        IconButton(onClick = onTrailingClick, modifier = Modifier.size(40.dp)) {
            AnimatedContent(targetState = expanded, label = "searchTrailingIcon") { isExpanded ->
                Icon(
                    if (isExpanded) Lucide.X else Lucide.ScanLine,
                    contentDescription = stringResource(if (isExpanded) R.string.clear_input else R.string.scan_qr),
                    tint = if (isExpanded) colors.foreground else colors.muted,
                    modifier = Modifier.size(20.dp),
                )
            }
        }
    }
}

@Composable
private fun SuggestionList(
    query: String,
    results: List<SearchSuggestion>,
    onNavigate: (SearchSuggestion) -> Unit,
) {
    val colors = LocalNovaColors.current
    LazyColumn(
        modifier = Modifier.fillMaxSize().padding(horizontal = 20.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(top = 8.dp, bottom = 24.dp),
    ) {
        if (query.isNotBlank()) {
            item {
                SuggestionRow(
                    title = AnnotatedString(stringResource(R.string.search_query, query)),
                    subtitle = "",
                    icon = Lucide.Search,
                    onClick = { onNavigate(SearchSuggestion(query, query, SuggestionType.Search)) },
                )
            }
            item { SectionLabel(stringResource(R.string.suggestions)) }
        } else {
            item { SectionLabel(stringResource(R.string.recent_visits)) }
        }
        if (results.isEmpty() && query.isNotBlank()) {
            item {
                Text(
                    stringResource(R.string.no_search_results),
                    modifier = Modifier.padding(horizontal = 6.dp, vertical = 14.dp),
                    color = colors.faint,
                    fontSize = 15.sp,
                )
            }
        } else if (results.isEmpty()) {
            item {
                Text(
                    stringResource(R.string.no_recent_visits),
                    modifier = Modifier.padding(horizontal = 6.dp, vertical = 14.dp),
                    color = colors.faint,
                    fontSize = 15.sp,
                )
            }
        } else {
            items(results) { suggestion ->
                SuggestionRow(
                    title = highlightedText(suggestion.title, query, colors.accent),
                    subtitle = if (suggestion.type == SuggestionType.Search) "" else suggestion.subtitle,
                    letter = suggestion.letter.takeIf { suggestion.type == SuggestionType.Site },
                    avatarColor = Color(suggestion.avatarColor),
                    icon = Lucide.Search.takeIf { suggestion.type == SuggestionType.Search },
                    onClick = { onNavigate(suggestion) },
                )
            }
        }
    }
}

private fun highlightedText(text: String, query: String, accent: Color): AnnotatedString {
    if (query.isBlank()) return AnnotatedString(text)
    val start = text.indexOf(query, ignoreCase = true)
    if (start < 0) return AnnotatedString(text)
    return buildAnnotatedString {
        append(text)
        addStyle(SpanStyle(color = accent, fontWeight = FontWeight.SemiBold), start, start + query.length)
    }
}

@Composable
private fun SuggestionRow(
    title: AnnotatedString,
    subtitle: String,
    onClick: () -> Unit,
    letter: String? = null,
    avatarColor: Color = LocalNovaColors.current.accent,
    icon: ImageVector? = null,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 6.dp, vertical = 6.dp)
            .height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (letter != null) LetterAvatar(letter, avatarColor, Modifier.size(34.dp))
        else Box(
            Modifier.size(34.dp).background(colors.surfaceSecondary, RoundedCornerShape(9.dp)),
            contentAlignment = Alignment.Center,
        ) { if (icon != null) Icon(icon, null, Modifier.size(18.dp)) }
        Spacer(Modifier.width(13.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = colors.foreground, fontSize = 15.sp, fontWeight = FontWeight.Medium, maxLines = 1)
            if (subtitle.isNotEmpty()) Text(subtitle, color = colors.faint, fontSize = 12.5.sp, maxLines = 1)
        }
        Icon(Lucide.ChevronRight, null, tint = colors.faint, modifier = Modifier.size(16.dp))
    }
}

@Composable
private fun ScannerOverlay(
    onClose: () -> Unit,
    onResult: (String) -> Unit,
) {
    val context = LocalContext.current
    var cameraGranted by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED,
        )
    }
    var cameraError by remember { mutableStateOf(false) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted -> cameraGranted = granted }
    val transition = rememberInfiniteTransition(label = "scanner")
    val laserPosition by transition.animateFloat(
        initialValue = 0.08f,
        targetValue = 0.92f,
        animationSpec = infiniteRepeatable(tween(1200), RepeatMode.Reverse),
        label = "laserPosition",
    )
    Box(
        Modifier
            .fillMaxSize()
            .background(Color(0xFF17181C))
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) awaitPointerEvent()
                }
            },
    ) {
        if (cameraGranted) {
            QrCameraPreview(
                onDecoded = onResult,
                onError = { cameraError = true },
                modifier = Modifier.fillMaxSize(),
            )
            Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = .28f)))
        }
        Row(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(R.string.scan_qr), color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            IconButton(
                onClick = onClose,
                modifier = Modifier.size(44.dp).border(1.dp, Color.White.copy(alpha = .2f), CircleShape),
            ) { Icon(Lucide.X, stringResource(R.string.close_scanner), tint = Color.White, modifier = Modifier.size(18.dp)) }
        }
        Canvas(Modifier.size(210.dp).align(Alignment.Center)) {
            val stroke = Stroke(width = 3.dp.toPx(), cap = StrokeCap.Round)
            drawRoundRect(Color.White, style = stroke, cornerRadius = androidx.compose.ui.geometry.CornerRadius(10.dp.toPx()))
            val y = size.height * laserPosition
            drawLine(Color.White, start = androidx.compose.ui.geometry.Offset(8.dp.toPx(), y), end = androidx.compose.ui.geometry.Offset(size.width - 8.dp.toPx(), y), strokeWidth = 2.dp.toPx(), cap = StrokeCap.Round)
        }
        if (!cameraGranted || cameraError) {
            Column(
                modifier = Modifier.align(Alignment.Center).padding(horizontal = 36.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    stringResource(if (cameraError) R.string.camera_unavailable else R.string.camera_permission_required),
                    color = Color.White,
                    fontSize = 14.sp,
                )
                if (!cameraGranted) {
                    Spacer(Modifier.height(16.dp))
                    Surface(
                        modifier = Modifier.height(40.dp).clickable {
                            permissionLauncher.launch(Manifest.permission.CAMERA)
                        },
                        shape = RoundedCornerShape(12.dp),
                        color = Color.White,
                    ) {
                        Box(Modifier.padding(horizontal = 18.dp), contentAlignment = Alignment.Center) {
                            Text(stringResource(R.string.allow_camera), color = Color.Black)
                        }
                    }
                }
            }
        }
        Text(
            stringResource(R.string.scanner_hint),
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 56.dp),
            color = Color.White.copy(alpha = .72f),
            fontSize = 13.5.sp,
        )
    }
}

@Composable
private fun BrowsingScreen(
    engine: Engine,
    controller: BrowserTabsController,
    tab: TabSessionState?,
    tabCount: Int,
    thumbnailCapture: BrowserThumbnailCapture,
    library: BrowserLibraryRepository,
    darkTheme: Boolean,
    onDarkThemeChange: (Boolean) -> Unit,
    onEditAddress: (String) -> Unit,
    onOpenTabs: () -> Unit,
    onHome: () -> Unit,
    onNavigate: (String) -> Unit,
    onOpen: (BrowserDestination) -> Unit,
) {
    val colors = LocalNovaColors.current
    val drawerState = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val outerDirection = LocalLayoutDirection.current
    val content = tab?.content
    val bookmarks by library.bookmarks.collectAsStateWithLifecycle()
    val isBookmarked = bookmarks.any { it.url == content?.url }
    var findOpen by remember { mutableStateOf(false) }
    var scannerOpen by remember { mutableStateOf(false) }
    var securitySheetOpen by remember { mutableStateOf(false) }
    var refreshRequested by remember { mutableStateOf(false) }
    var refreshObservedLoading by remember { mutableStateOf(false) }

    LaunchedEffect(tab?.id) {
        refreshRequested = false
        refreshObservedLoading = false
    }

    LaunchedEffect(refreshRequested, content?.loading) {
        if (!refreshRequested) return@LaunchedEffect
        if (content?.loading == true) {
            refreshObservedLoading = true
        } else if (refreshObservedLoading) {
            refreshRequested = false
            refreshObservedLoading = false
        }
    }

    BackHandler(enabled = scannerOpen) {
        scannerOpen = false
    }

    BackHandler(enabled = findOpen) {
        findOpen = false
        controller.clearFindMatches()
    }

    BackHandler(enabled = !scannerOpen && !findOpen && content?.canGoBack == true) {
        controller.goBack()
    }

    androidx.compose.runtime.CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) {
        ModalNavigationDrawer(
            drawerState = drawerState,
            gesturesEnabled = drawerState.isOpen,
            drawerContent = {
                androidx.compose.runtime.CompositionLocalProvider(LocalLayoutDirection provides outerDirection) {
                    ModalDrawerSheet(
                        modifier = Modifier.fillMaxHeight().width(300.dp),
                        drawerContainerColor = colors.surface,
                    ) {
                        BrowserDrawer(
                            controller = controller,
                            canGoBack = content?.canGoBack == true,
                            canGoForward = content?.canGoForward == true,
                            isBookmarked = isBookmarked,
                            darkTheme = darkTheme,
                            onDarkThemeChange = onDarkThemeChange,
                            onToggleBookmark = {
                                scope.launch {
                                    runCatching {
                                        library.toggleBookmark(
                                            content?.url.orEmpty(),
                                            content?.let { it.title.ifBlank { it.url } }.orEmpty(),
                                        )
                                    }
                                }
                            },
                            onFind = {
                                scope.launch { drawerState.close() }
                                findOpen = true
                            },
                            onReload = {
                                scope.launch {
                                    drawerState.close()
                                    controller.reload()
                                }
                            },
                            onShare = {
                                val intent = Intent(Intent.ACTION_SEND).apply {
                                    type = "text/plain"
                                    putExtra(Intent.EXTRA_TEXT, content?.url.orEmpty())
                                }
                                context.startActivity(Intent.createChooser(intent, null))
                            },
                            onHome = {
                                scope.launch {
                                    drawerState.close()
                                    onHome()
                                }
                            },
                            onScan = {
                                scope.launch {
                                    drawerState.close()
                                    scannerOpen = true
                                }
                            },
                            onOpen = { target ->
                                scope.launch { drawerState.close() }
                                onOpen(target)
                            },
                        )
                    }
                }
            },
        ) {
            androidx.compose.runtime.CompositionLocalProvider(LocalLayoutDirection provides outerDirection) {
                Box(Modifier.fillMaxSize()) {
                    Column(
                        Modifier
                            .fillMaxSize()
                            .windowInsetsPadding(WindowInsets.safeDrawing)
                            .background(colors.surface),
                    ) {
                        AddressBar(
                            url = content?.url.orEmpty(),
                            isSecure = content?.securityInfo?.isSecure == true,
                            tabCount = tabCount,
                            loading = content?.loading == true,
                            progress = content?.progress ?: 0,
                            onSecurityClick = { securitySheetOpen = true },
                            onAddressClick = { onEditAddress(content?.url.orEmpty()) },
                            onTabs = onOpenTabs,
                            onMenu = { scope.launch { drawerState.open() } },
                        )
                        if (findOpen) {
                            FindInPageBar(
                                result = content?.findResults?.lastOrNull(),
                                onQueryChange = { query ->
                                    if (query.isBlank()) controller.clearFindMatches() else controller.findAll(query)
                                },
                                onPrevious = { controller.findNext(false) },
                                onNext = { controller.findNext(true) },
                                onClose = {
                                    findOpen = false
                                    controller.clearFindMatches()
                                },
                            )
                        }
                        val session = tab?.engineState?.engineSession
                        if (session != null) {
                            Box(Modifier.weight(1f).fillMaxWidth()) {
                                BrowserSurface(
                                    engine = engine,
                                    session = session,
                                    modifier = Modifier.fillMaxSize().testTag(BROWSER_SURFACE_TEST_TAG),
                                    thumbnailCapture = thumbnailCapture,
                                    refreshing = refreshRequested,
                                    onRefresh = {
                                        refreshRequested = true
                                        refreshObservedLoading = false
                                        controller.reload()
                                    },
                                )
                                EdgeBackGesture(
                                    enabled = content?.canGoBack == true && drawerState.isClosed,
                                    onBack = controller::goBack,
                                    modifier = Modifier.align(Alignment.CenterStart),
                                )
                            }
                        } else {
                            Box(Modifier.weight(1f).fillMaxWidth())
                        }
                    }
                    if (scannerOpen) {
                        ScannerOverlay(
                            onClose = { scannerOpen = false },
                            onResult = { result ->
                                scannerOpen = false
                                onNavigate(result)
                            },
                        )
                    }
                }
            }
        }
    }
    if (securitySheetOpen) {
        SiteSecurityBottomSheet(
            details = SiteSecurityDetails.from(
                url = content?.url.orEmpty(),
                securityInfo = content?.securityInfo,
            ),
            onDismiss = { securitySheetOpen = false },
        )
    }
}

@Composable
internal fun EdgeBackGesture(
    enabled: Boolean,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!enabled) return

    Box(
        modifier = modifier
            .fillMaxHeight()
            .width(EdgeBackGestureWidth)
            .testTag(EDGE_BACK_GESTURE_TEST_TAG)
            .pointerInput(onBack) {
                var distance = 0f
                detectHorizontalDragGestures(
                    onDragStart = { distance = 0f },
                    onHorizontalDrag = { change, dragAmount ->
                        distance = (distance + dragAmount).coerceAtLeast(0f)
                        change.consume()
                    },
                    onDragEnd = {
                        if (distance >= EdgeBackGestureThreshold.toPx()) onBack()
                    },
                    onDragCancel = { distance = 0f },
                )
            },
    )
}

@Composable
private fun FindInPageBar(
    result: mozilla.components.browser.state.state.content.FindResultState?,
    onQueryChange: (String) -> Unit,
    onPrevious: () -> Unit,
    onNext: () -> Unit,
    onClose: () -> Unit,
) {
    val colors = LocalNovaColors.current
    var query by remember { mutableStateOf("") }
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp)
            .height(48.dp)
            .background(colors.surfaceSecondary, RoundedCornerShape(12.dp))
            .border(1.dp, colors.border, RoundedCornerShape(12.dp))
            .padding(start = 14.dp, end = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        BasicTextField(
            value = query,
            onValueChange = {
                query = it
                onQueryChange(it)
            },
            modifier = Modifier.weight(1f),
            singleLine = true,
            textStyle = androidx.compose.ui.text.TextStyle(color = colors.foreground, fontSize = 14.sp),
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            keyboardActions = KeyboardActions(onSearch = { if (query.isNotBlank()) onNext() }),
            decorationBox = { inner ->
                if (query.isEmpty()) Text(stringResource(R.string.find_in_page_hint), color = colors.faint, fontSize = 14.sp)
                inner()
            },
        )
        Text(
            stringResource(
                R.string.find_matches,
                result?.activeMatchOrdinal ?: 0,
                result?.numberOfMatches ?: 0,
            ),
            color = colors.muted,
            fontSize = 12.sp,
            fontFamily = FontFamily.Monospace,
        )
        IconButton(onClick = onPrevious, enabled = (result?.numberOfMatches ?: 0) > 0, modifier = Modifier.size(40.dp)) {
            Icon(Lucide.ChevronUp, stringResource(R.string.previous_match), modifier = Modifier.size(18.dp))
        }
        IconButton(onClick = onNext, enabled = (result?.numberOfMatches ?: 0) > 0, modifier = Modifier.size(40.dp)) {
            Icon(Lucide.ChevronDown, stringResource(R.string.next_match), modifier = Modifier.size(18.dp))
        }
        IconButton(onClick = onClose, modifier = Modifier.size(40.dp)) {
            Icon(Lucide.X, stringResource(R.string.close_find), modifier = Modifier.size(18.dp))
        }
    }
}

@Composable
internal fun AddressBar(
    url: String,
    isSecure: Boolean,
    tabCount: Int,
    loading: Boolean,
    progress: Int,
    onSecurityClick: () -> Unit,
    onAddressClick: () -> Unit,
    onTabs: () -> Unit,
    onMenu: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val shape = RoundedCornerShape(12.dp)
    val animatedProgress by animateFloatAsState(
        targetValue = if (loading) progress.coerceIn(0, 100) / 100f else 0f,
        animationSpec = tween(durationMillis = 180),
        label = "addressLoadProgress",
    )
    val tabSwitcherDescription = stringResource(R.string.tab_switcher)
    val securityInteractionSource = remember { MutableInteractionSource() }
    val uri = remember(url) { runCatching { Uri.parse(url) }.getOrNull() }
    val host = uri?.host ?: url.ifBlank { "about:blank" }
    val path = uri?.encodedPath.orEmpty().takeUnless { it == "/" }.orEmpty()
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .testTag(BROWSER_CONTROLS_TEST_TAG)
            .padding(horizontal = 12.dp, vertical = 10.dp)
            .height(44.dp)
            .clip(shape)
            .background(colors.surfaceSecondary, shape)
            .border(1.dp, colors.border, shape),
    ) {
        Box(
            Modifier
                .fillMaxHeight()
                .fillMaxWidth(animatedProgress)
                .background(colors.accent.copy(alpha = .08f)),
        )
        Row(
            modifier = Modifier.fillMaxSize().padding(start = 4.dp, end = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .width(36.dp)
                    .clickable(
                        interactionSource = securityInteractionSource,
                        indication = null,
                        onClick = onSecurityClick,
                    ),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    modifier = Modifier
                        .size(28.dp)
                        .clip(CircleShape)
                        .indication(securityInteractionSource, ripple())
                        .testTag(SECURITY_RIPPLE_TEST_TAG),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        if (isSecure) Lucide.Lock else Lucide.Globe,
                        contentDescription = stringResource(
                            if (isSecure) R.string.secure_connection else R.string.connection_information,
                        ),
                        tint = if (isSecure) colors.success else colors.muted,
                        modifier = Modifier.size(15.dp),
                    )
                }
            }
            Box(
                modifier = Modifier.weight(1f).fillMaxHeight().clickable(onClick = onAddressClick),
                contentAlignment = Alignment.CenterStart,
            ) {
                Text(
                    buildAnnotatedString {
                        append(host)
                        addStyle(SpanStyle(color = colors.foreground, fontWeight = FontWeight.SemiBold), 0, host.length)
                        append(path)
                        addStyle(SpanStyle(color = colors.muted), host.length, host.length + path.length)
                    },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    fontSize = 14.sp,
                )
            }
            TabCountButton(
                tabCount = tabCount,
                contentDescription = tabSwitcherDescription,
                onClick = onTabs,
                modifier = Modifier.size(32.dp),
            )
            IconButton(onClick = onMenu, modifier = Modifier.size(32.dp)) {
                Icon(Lucide.Ellipsis, stringResource(R.string.menu), tint = colors.muted, modifier = Modifier.size(19.dp))
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun SiteSecurityBottomSheet(
    details: SiteSecurityDetails,
    onDismiss: () -> Unit,
) {
    val colors = LocalNovaColors.current
    val (stateIcon, stateLabel, stateColor) = when (details.state) {
        SiteSecurityState.SECURE -> Triple(Lucide.Lock, R.string.connection_is_secure, colors.success)
        SiteSecurityState.INSECURE -> Triple(Lucide.Globe, R.string.connection_is_not_secure, colors.danger)
        SiteSecurityState.UNAVAILABLE -> Triple(Lucide.Globe, R.string.no_connection_information, colors.muted)
    }
    val certificateRows = listOfNotNull(
        details.subject?.let { R.string.certificate_subject to it },
        details.issuer?.let { R.string.certificate_issuer to it },
        details.validFrom?.let { R.string.certificate_valid_from to it },
        details.validUntil?.let { R.string.certificate_valid_until to it },
        details.serialNumber?.let { R.string.certificate_serial_number to it },
        details.sha256Fingerprint?.let { R.string.certificate_sha256_fingerprint to it },
    )

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        containerColor = colors.surface,
        contentColor = colors.foreground,
        shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp)
                .padding(bottom = 32.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.site_information),
                        color = colors.muted,
                        fontSize = 13.sp,
                    )
                    Text(
                        text = details.host,
                        color = colors.foreground,
                        fontSize = 20.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                IconButton(onClick = onDismiss) {
                    Icon(Lucide.X, stringResource(R.string.close_site_information))
                }
            }
            Row(
                modifier = Modifier.padding(top = 20.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Surface(
                    modifier = Modifier.size(40.dp),
                    shape = CircleShape,
                    color = stateColor.copy(alpha = .12f),
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(stateIcon, null, tint = stateColor, modifier = Modifier.size(19.dp))
                    }
                }
                Text(
                    text = stringResource(stateLabel),
                    modifier = Modifier.padding(start = 12.dp),
                    color = colors.foreground,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                )
            }
            if (certificateRows.isNotEmpty()) {
                HorizontalDivider(Modifier.padding(vertical = 24.dp), color = colors.border)
                Text(
                    text = stringResource(R.string.certificate_information),
                    color = colors.foreground,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                )
                SelectionContainer {
                    Column(Modifier.padding(top = 8.dp)) {
                        certificateRows.forEach { (label, value) ->
                            CertificateDetailRow(label = stringResource(label), value = value)
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun CertificateDetailRow(label: String, value: String) {
    val colors = LocalNovaColors.current
    Column(Modifier.fillMaxWidth().padding(vertical = 9.dp)) {
        Text(text = label, color = colors.muted, fontSize = 12.sp)
        Text(
            text = value,
            modifier = Modifier.padding(top = 3.dp),
            color = colors.foreground,
            fontSize = 14.sp,
            lineHeight = 20.sp,
        )
    }
}

@Composable
private fun TabCountButton(
    tabCount: Int,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Box(
        modifier = modifier
            .semantics(mergeDescendants = true) {
                this.contentDescription = contentDescription
            }
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Surface(
            modifier = Modifier.size(24.dp),
            color = Color.Transparent,
            shape = RoundedCornerShape(6.dp),
            border = BorderStroke(2.dp, colors.foreground),
        ) {
            Box(contentAlignment = Alignment.Center) {
                Text(
                    tabCount.toString(),
                    color = colors.foreground,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold,
                )
            }
        }
    }
}

@Composable
private fun BrowserDrawer(
    controller: BrowserTabsController,
    canGoBack: Boolean,
    canGoForward: Boolean,
    isBookmarked: Boolean,
    darkTheme: Boolean,
    onDarkThemeChange: (Boolean) -> Unit,
    onToggleBookmark: () -> Unit,
    onFind: () -> Unit,
    onReload: () -> Unit,
    onShare: () -> Unit,
    onHome: () -> Unit,
    onScan: () -> Unit,
    onOpen: (BrowserDestination) -> Unit,
) {
    val colors = LocalNovaColors.current
    Column(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
        Row(
            modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            DrawerNavButton(Lucide.ChevronLeft, R.string.back, controller::goBack, Modifier.weight(1f), canGoBack)
            DrawerNavButton(Lucide.ChevronRight, R.string.forward, controller::goForward, Modifier.weight(1f), canGoForward)
            DrawerNavButton(Lucide.RefreshCw, R.string.reload, onReload, Modifier.weight(1f))
            DrawerNavButton(Lucide.Share, R.string.share, onShare, Modifier.weight(1f))
        }
        Row(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            DrawerBookmarkTile(
                isBookmarked = isBookmarked,
                modifier = Modifier.weight(1f),
                onToggleBookmark = onToggleBookmark,
            )
            DrawerTile(Lucide.SearchCode, R.string.find_in_page, Modifier.weight(1f), onFind)
        }
        HorizontalDivider(Modifier.padding(horizontal = 16.dp, vertical = 14.dp), color = colors.border)
        DrawerItem(Lucide.House, R.string.home, onHome)
        DrawerItem(Lucide.ScanLine, R.string.scan_qr, onScan)
        DrawerItem(Lucide.History, R.string.history) { onOpen(BrowserDestination.History) }
        DrawerItem(Lucide.Bookmark, R.string.bookmarks) { onOpen(BrowserDestination.Bookmarks) }
        DrawerItem(Lucide.Download, R.string.downloads) { onOpen(BrowserDestination.Downloads) }
        DrawerItem(Lucide.Puzzle, R.string.extensions) { onOpen(BrowserDestination.Extensions) }
        DrawerItem(
            if (darkTheme) Lucide.Sun else Lucide.Moon,
            R.string.dark_mode,
        ) { onDarkThemeChange(!darkTheme) }
        DrawerItem(Lucide.Settings, R.string.settings) { onOpen(BrowserDestination.Settings) }
    }
}

@Composable
private fun DrawerNavButton(
    icon: ImageVector,
    labelRes: Int,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val colors = LocalNovaColors.current
    Surface(
        onClick = onClick,
        modifier = modifier.height(46.dp),
        enabled = enabled,
        color = colors.surfaceSecondary.copy(alpha = if (enabled) 1f else .55f),
        shape = RoundedCornerShape(12.dp),
        border = BorderStroke(1.dp, colors.border.copy(alpha = if (enabled) 1f else .55f)),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                icon,
                stringResource(labelRes),
                Modifier.size(20.dp),
                tint = if (enabled) colors.foreground else colors.faint,
            )
        }
    }
}

@Composable
internal fun DrawerBookmarkTile(
    isBookmarked: Boolean,
    modifier: Modifier = Modifier,
    onToggleBookmark: () -> Unit,
) {
    DrawerTile(
        icon = if (isBookmarked) FilledStarIcon else Lucide.Star,
        labelRes = if (isBookmarked) R.string.remove_bookmark else R.string.add_bookmark,
        modifier = modifier
            .testTag(BOOKMARK_DRAWER_TILE_TEST_TAG)
            .semantics { selected = isBookmarked },
        iconModifier = Modifier.testTag(BOOKMARK_DRAWER_ICON_TEST_TAG),
        onClick = onToggleBookmark,
    )
}

@Composable
private fun DrawerTile(
    icon: ImageVector,
    labelRes: Int,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
    iconModifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Surface(
        modifier = modifier.height(92.dp).clickable(onClick = onClick),
        color = colors.surfaceSecondary,
        shape = RoundedCornerShape(13.dp),
        border = BorderStroke(1.dp, colors.border),
    ) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.SpaceBetween) {
            Icon(icon, null, iconModifier.size(22.dp))
            Text(stringResource(labelRes), fontSize = 13.sp, fontWeight = FontWeight.Medium)
        }
    }
}

@Composable
private fun DrawerItem(icon: ImageVector, labelRes: Int, onClick: () -> Unit) {
    val colors = LocalNovaColors.current
    Row(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(horizontal = 20.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(icon, null, tint = colors.muted, modifier = Modifier.size(21.dp))
        Spacer(Modifier.width(13.dp))
        Text(stringResource(labelRes), color = colors.foreground, fontSize = 14.5.sp, fontWeight = FontWeight.Medium)
    }
}
