package com.msgbyte.dao.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class BrowserScreenTransitionTest {
    @Test
    fun `forward utility navigation slides forward`() {
        listOf(
            BrowserDestination.Settings,
            BrowserDestination.History,
            BrowserDestination.Bookmarks,
            BrowserDestination.Downloads,
            BrowserDestination.Extensions,
        ).forEach { destination ->
            assertEquals(
                BrowserScreenTransition.Forward,
                resolveBrowserScreenTransition(
                    from = BrowserDestination.Browsing,
                    to = destination,
                    direction = BrowserNavigationDirection.Forward,
                ),
            )
        }
    }

    @Test
    fun `nested navigation uses explicit direction`() {
        assertEquals(
            BrowserScreenTransition.Forward,
            resolveBrowserScreenTransition(
                BrowserDestination.Settings,
                BrowserDestination.About,
                BrowserNavigationDirection.Forward,
            ),
        )
        assertEquals(
            BrowserScreenTransition.Backward,
            resolveBrowserScreenTransition(
                BrowserDestination.About,
                BrowserDestination.Settings,
                BrowserNavigationDirection.Backward,
            ),
        )
        assertEquals(
            BrowserScreenTransition.Forward,
            resolveBrowserScreenTransition(
                BrowserDestination.About,
                BrowserDestination.OpenSourceLicenses,
                BrowserNavigationDirection.Forward,
            ),
        )
        assertEquals(
            BrowserScreenTransition.Backward,
            resolveBrowserScreenTransition(
                BrowserDestination.AmoStore,
                BrowserDestination.Extensions,
                BrowserNavigationDirection.Backward,
            ),
        )
    }

    @Test
    fun `tab grid entry and exit override directional slides`() {
        assertEquals(
            BrowserScreenTransition.OpenTabGrid,
            resolveBrowserScreenTransition(
                BrowserDestination.Browsing,
                BrowserDestination.Tabs,
                BrowserNavigationDirection.Immediate,
            ),
        )
        assertEquals(
            BrowserScreenTransition.CloseTabGrid,
            resolveBrowserScreenTransition(
                BrowserDestination.Tabs,
                BrowserDestination.NewTab,
                BrowserNavigationDirection.Immediate,
            ),
        )
    }

    @Test
    fun `primary browser changes remain immediate`() {
        val primaryDestinations = listOf(
            BrowserDestination.NewTab,
            BrowserDestination.Browsing,
            BrowserDestination.AddressEdit,
        )

        primaryDestinations.forEach { from ->
            primaryDestinations.forEach { to ->
                assertEquals(
                    BrowserScreenTransition.Immediate,
                    resolveBrowserScreenTransition(
                        from,
                        to,
                        BrowserNavigationDirection.Forward,
                    ),
                )
            }
        }
    }

    @Test
    fun `extension action returning to browsing remains immediate`() {
        assertEquals(
            BrowserScreenTransition.Immediate,
            resolveBrowserScreenTransition(
                BrowserDestination.Extensions,
                BrowserDestination.Browsing,
                BrowserNavigationDirection.Immediate,
            ),
        )
    }

    @Test
    fun `same destination never animates`() {
        BrowserDestination.entries.forEach { destination ->
            assertEquals(
                BrowserScreenTransition.Immediate,
                resolveBrowserScreenTransition(
                    destination,
                    destination,
                    BrowserNavigationDirection.Forward,
                ),
            )
        }
    }
}
