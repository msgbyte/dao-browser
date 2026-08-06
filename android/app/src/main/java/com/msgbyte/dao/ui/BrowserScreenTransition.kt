package com.msgbyte.dao.ui

internal enum class BrowserNavigationDirection {
    Immediate,
    Forward,
    Backward,
}

internal enum class BrowserScreenTransition {
    Immediate,
    Forward,
    Backward,
    OpenTabGrid,
    CloseTabGrid,
}

internal data class BrowserAnimatedDestination(
    val destination: BrowserDestination,
    val transition: BrowserScreenTransition,
)

private val immediateBrowserDestinations = setOf(
    BrowserDestination.NewTab,
    BrowserDestination.AddressEdit,
    BrowserDestination.Browsing,
)

internal fun resolveBrowserScreenTransition(
    from: BrowserDestination,
    to: BrowserDestination,
    direction: BrowserNavigationDirection,
): BrowserScreenTransition = when {
    from == to -> BrowserScreenTransition.Immediate
    to == BrowserDestination.Tabs -> BrowserScreenTransition.OpenTabGrid
    from == BrowserDestination.Tabs -> BrowserScreenTransition.CloseTabGrid
    from in immediateBrowserDestinations && to in immediateBrowserDestinations ->
        BrowserScreenTransition.Immediate
    direction == BrowserNavigationDirection.Forward -> BrowserScreenTransition.Forward
    direction == BrowserNavigationDirection.Backward -> BrowserScreenTransition.Backward
    else -> BrowserScreenTransition.Immediate
}
