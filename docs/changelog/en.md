# Changelog

## [desktop] Desktop

### [Unreleased] Unreleased

- Keep readable release notes in English and Chinese for every change, with separate histories for desktop, Android, and iOS.
- Reconstruct desktop and Android release notes from Git tags, checking each version, adding feature details, and rewriting the explanations.

### [1.0.108] - 2026-09-22

- Drag tabs between windows or onto the desktop to open a new window, keeping their page state and history.
- Dragging a window's only tab moves the window itself; pressing Escape cancels the drag.
- Move a background tab to a new window from its context menu. The action is unavailable for a window's last tab.
- Moving a pinned tab between windows no longer treats the move as closing the page.
- Folder changes stay in sync across windows, and folder membership survives browser restarts.
- Add a draft privacy policy covering desktop, Android, and iOS.

### [1.0.107] - 2026-09-19

- Run browser actions in batches with fewer round trips; if an action fails, later actions stop.
- Add guarded fill, key, and wait actions for browser automation.
- When the MCP client limit is reached, idle connections can make room for a new client.
- Background-page loading no longer makes the active page's loading interface flash.
- Closing duplicate tabs reports how many actually closed; tabs kept open after a leave-page prompt are not counted.
- Keep the MCP usage list within a scrollable area instead of letting it grow indefinitely.

### [1.0.106] - 2026-09-18

- Give the sidebar update button more appropriate spacing.
- Remove the misleading verified-process field from MCP details.
- Improve website metadata and make pages easier for search engines to crawl.

### [1.0.105] - 2026-09-12

- Remove attached context while editing a message.
- Edited messages and retries keep their browser-control capabilities.
- MCP permission requests show more context about the request.
- Dream report cards show the report summary.
- Split-view dividers and corners now match the window frame's colors.
- Change the parallel-download setting while the browser is running.

### [1.0.104] - 2026-09-07

- Enable command mode in settings, then enter `>` in the command bar to list or filter commands. It is off by default.
- Use the command bar to open Settings, Downloads, History, extensions, Agent settings, or Task Manager, reopen a closed tab, or copy the current link.
- Opening the command bar with Cmd+T waits until you choose an action before creating a tab.
- A sidebar animation confirms when a download starts; a toast provides feedback when the sidebar is collapsed.
- Download-start feedback respects reduced-motion preferences.

### [1.0.103] - 2026-09-06

- Closing the current tab skips stale tabs when choosing which tab to activate next.
- Import browser data from the welcome page, and follow the tutorial in the selected interface language.
- Refresh desktop translations and let the translation tooling use an alternate API-key variable and gateway requests.

### [1.0.102] - 2026-09-04

- The MCP control popup shows the most recent tool call.
- Archive older download files on GitHub so past releases remain available.

### [1.0.101] - 2026-09-03

- MCP clients can find page elements by name, text, or role, and recheck the target before clicking so a changed page does not trigger an outdated action.
- Wait for a matching network response before continuing a browser workflow.
- View MCP tool-usage statistics.
- When MCP is enabled, copy a regular tab's ID from its sidebar menu to target it in external tools.

### [1.0.100] - 2026-09-02

- The sidebar marks tabs currently controlled through MCP.

### [1.0.99] - 2026-09-01

- Track time spent actively browsing a foreground page and use it in Dream reports.
- Activity records store sites rather than full URLs or page titles, and exclude incognito browsing.
- Reports state how much activity the records cover, so a partial record is not presented as a complete browsing history.

### [1.0.98] - 2026-08-31

- Stop Agent actions from targeting pages that have already been detached.
- Keep the current-page context associated with each MCP client.
- Pinned-tab warnings use Dao's own toast messages.

### [1.0.97] - 2026-08-31

- The sidebar keeps the completed-download message visible after a download finishes.

### [1.0.96] - 2026-08-27

- The Agent can operate background tabs without requiring each one to be brought to the front.
- A native action cursor makes automated browser actions visible.
- Different MCP clients keep separate control of their tabs.
- Check that page content is available before running automated actions.
- Respect the browser's native automatic Picture-in-Picture setting.

### [1.0.95] - 2026-08-25

- The address bar marks insecure pages.
- Hover over the assistant entry to see its keyboard shortcut.

### [1.0.94] - 2026-08-24

- Scroll through the full command-bar suggestion list, including items beyond the visible area.
- Fix titlebar controls that could not be clicked beside an open side panel.

### [1.0.93] - 2026-08-21

- Open `dao://home` to create a personalized homepage with the Agent; it does not replace the default new-tab page automatically.
- Generated homepages must pass a preview check before publication.
- Approve access to the data sources used by the homepage; approved sources are read-only.
- Opening a published homepage does not trigger another model call, and you can request a repair if needed.
- Show Agent tool calls more clearly.
- Keep exact-search actions available and make suggestion selection agree with command-bar completion.
- Refine the address-bar divider, disable the Lens overlay, and update dependencies with known security issues.

### [1.0.92] - 2026-08-14

- Switching tabs no longer closes webpage Picture-in-Picture.
- Improve the Dream history list and its appearance in dark mode.
- Repair failed patch applications during rebuilds.

### [1.0.91] - 2026-08-13

- Import browsing data from Chrome, Arc, Edge, Safari, and Firefox; the available categories depend on the source browser.
- Keep existing Dao data during import, follow progress, and retry failed parts without restarting the whole migration.
- Imported tabs are grouped in a collapsed folder to keep the sidebar manageable.
- Open Agent settings through a more compact entry.
- Set the report language before generating a Dream report.
- Update website dependencies with known security issues.

### [1.0.90] - 2026-08-10

- Reorganize settings and Agent management.
- Dream reports use a one-minute recap layout for a quicker review.

### [1.0.89] - 2026-08-09

- Move tabs out of folders, and confirm before deleting a folder.
- Fix navigation in Dream reports.
- Retry the latest model-provider error without submitting the user's message twice.

### [1.0.88] - 2026-08-06

- Check documentation consistency and run fast CI checks before deploying the website.

### [1.0.87] - 2026-08-04

- Hover over an active download to see its details.

### [1.0.86] - 2026-08-03

- Press Right Arrow to fill the currently selected command-bar suggestion.
- Restore the original webpage element when returning from webpage Picture-in-Picture.
- Mini Dao stays windowed even when opened from a fullscreen browser.

### [1.0.85] - 2026-08-01

- Expand a collapsed folder before scrolling to its active tab.
- Explain why forced dark mode is unavailable instead of leaving the action without feedback.
- Refresh the website's dark appearance.

### [1.0.84] - 2026-07-31

- External tools can connect to Dao through MCP to work with browser pages.
- Activating a pinned tab already open in another window reuses that page, and closing it keeps the pinned state in sync.
- Make extension badges and their backgrounds more compact.

### [1.0.83] - 2026-07-29

- Choose how long a tab can remain inactive before it becomes stale.
- Clear the tab-drag state when the mouse button is released.
- Make forced patch cleanup safe to run more than once.

### [1.0.82] - 2026-07-28

- Clear stale tabs from a dedicated action.
- Keep hover feedback working on pinned extension buttons.
- Correct the appearance of the message area in alert dialogs.

### [1.0.81] - 2026-07-24

- An indicator in the sidebar makes incognito windows easier to recognize.
- Extension buttons can display action badges.
- Upgrade the Agent runtime and pass screenshots to the model as images.

### [1.0.80] - 2026-07-23

- Upgrade the browser engine to Chromium 149.0.7827.201.
- Fix pages no longer responding to mouse input after dragging a split-view tab.

### [1.0.79] - 2026-07-20

- Keep the current tab visible in the sidebar.
- Show Dao's own confirmation toast after copying an image.

### [1.0.78] - 2026-07-20

- Dream can now create weekly browsing reports.
- Pressing Enter in the command bar no longer brings back a gray-text completion you already rejected.
- Fix Chromium 148 build compatibility and remove temporary disk images after packaging.

### [1.0.77] - 2026-07-17

- Restored browser sessions keep pinned tabs associated with the correct pages.

### [1.0.76] - 2026-07-15

- Keep separate tabs distinct even when they open the same page, so sidebar synchronization does not mix them up.
- Update interface translations.

### [1.0.74] - 2026-07-14

- Tab context menus show the keyboard shortcuts for their actions.
- Release-file cleanup estimates how much disk space it will free.

### [1.0.73] - 2026-07-13

- Resize Picture-in-Picture across the full available desktop area.
- Check the Chromium version before importing patches, and restore version changes if a release fails.

### [1.0.72] - 2026-07-12

- Fix a build error caused by external undici type declarations.

### [1.0.71] - 2026-07-11

- Upgrade the browser engine to Chromium 148.
- Webpage JavaScript dialogs now use Dao's visual style.
- Improve the controls for excluding source sites from Dream reports.

### [1.0.70] - 2026-07-09

- This tag uses the same source commit as 1.0.69, with no additional code changes.

### [1.0.69] - 2026-07-09

- Web search can fall back to Jina when the primary path is unavailable.
- Choose whether the command bar shows Ask AI suggestions.
- Clearing site data affects only the intended site.
- Make disabled labels in the control center easier to read.

### [1.0.68] - 2026-07-08

- This tag uses the same source commit as 1.0.67, with no additional code changes.

### [1.0.67] - 2026-07-08

- Fix an Agent TypeScript type-checking error that blocked the build.

### [1.0.66] - 2026-07-08

- This tag uses the same source commit as 1.0.65, with no additional code changes.

### [1.0.65] - 2026-07-08

- Force websites into dark mode from the browser controls.
- Generated QR codes use Dao styling and include the site's icon.
- Agent conversations automatically condense older messages when approaching the context limit.
- Menus and tab tooltips update correctly when the theme changes.
- Fix stale extension icons and arrow-key handling in webpage Picture-in-Picture.

### [1.0.64] - 2026-07-07

- This tag uses the same source commit as 1.0.63, with no additional code changes.

### [1.0.63] - 2026-07-07

- Attach a screenshot of a selected webpage element to the Agent conversation.
- Scrolling chat history stays inside the history panel.
- Preview enhanced Picture-in-Picture in settings before choosing whether to enable it.
- Switch the project license to BSD-3-Clause and expand shared-engine maintenance tools.

### [1.0.62] - 2026-07-06

- Fix Bilibili webpage Picture-in-Picture closing unexpectedly.
- Clear control-popup hover highlights when they are no longer active.
- Exclude content from Dream reports and regenerate a report when needed.
- Add tooling to configure engine caching for development.

### [1.0.61] - 2026-07-05

- Fix webpage Picture-in-Picture closing immediately after it opens.
- Improve which release files are selected during upload cleanup.

### [1.0.60] - 2026-07-04

- Only release information changed after 1.0.59; there are no additional browser behavior changes.

### [1.0.59] - 2026-07-04

- Refine proactive suggestions based on memory.
- Little Dao gives clearer feedback after copying a URL.
- Display the full URL path and keep Mini Dao in windowed mode.
- Clear stale webpage Picture-in-Picture state so it does not linger after use.
- Improve the website's search-engine metadata.

### [1.0.58] - 2026-07-03

- Little Dao shows a card with the progress of an active download.

### [1.0.57] - 2026-07-02

- Control Picture-in-Picture from the sidebar.
- Improve opening, closing, and retry handling for webpage Picture-in-Picture.

### [1.0.56] - 2026-07-02

- Move a tab into a Mini Dao window and access its site controls there.
- Retain release debug symbols locally to help investigate crashes.

### [1.0.55] - 2026-07-01

- Pinned tabs appear as compact icons; hover over one to see its tooltip.
- Align the downloads popup with the sidebar edges.

### [1.0.54] - 2026-06-30

- Return to an earlier assistant reply and continue the conversation from there.
- Improve imports when some parts of a Chromium patch are already applied.

### [1.0.53] - 2026-06-29

- This tag uses the same source commit as 1.0.52, with no additional code changes.

### [1.0.52] - 2026-06-29

- Command-bar suggestions distinguish searching, opening an address, switching tabs, and asking Dao.
- Enter a local file path in the command bar to open it.
- Little Dao remembers its resized window bounds while keeping them within a usable area.
- Correct the direction of the Agent send icon.

### [1.0.51] - 2026-06-28

- Keep pinned tabs associated with the correct pages; duplicating one creates a regular tab.
- Remember webpage Picture-in-Picture size and position separately for each site.
- Improve translations in settings.

### [1.0.50] - 2026-06-26

- Relevant Agent skills activate automatically when needed.
- Close duplicate tabs with a single action.

### [1.0.49] - 2026-06-25

- Fix how external links open in Little Dao, and add a setting to turn Little Dao on or off.
- Refresh extension action icons correctly and update interface translations.

### [1.0.48] - 2026-06-23

- Folders use native context menus, and tab-menu labels now have translations.
- Proactive suggestion cards work better in dark mode and across interface languages.
- Keep message popups within the screen edges.
- Add a context inspector to help debug what information the Agent receives.

### [1.0.47] - 2026-06-22

- Use the sidebar or tab context menu to move eligible tabs unused for more than 24 hours into the stale folder, where you can still find them.
- Refresh translations and consolidate duplicate Chromium patches for easier maintenance.

### [1.0.46] - 2026-06-21

- Pinned tabs stay compact even when there are only a few of them.

### [1.0.45] - 2026-06-20

- Dream reports use preferences you have confirmed.
- Share a Dream report as an image, and catch up on missed reports after the Agent resumes.
- Fix unreliable tab closing from the sidebar.
- Show report-generation time in the debug view.

### [1.0.44] - 2026-06-19

- Edit chat messages and access message actions from a dedicated row of controls.
- Dismiss a Dream report card or open past reports from the report controls.
- Dream reports consider how long you spent on a page when weighing browsing material.

### [1.0.43] - 2026-06-18

- Internal memory context no longer appears as visible chat content.
- Keep webpage content aligned when opening or closing the sidebar.
- Refresh translations and explain confirmation-based Agent suggestions on the website.

### [1.0.42] - 2026-06-18

- Only release information changed after 1.0.41; there are no additional browser behavior changes.

### [1.0.41] - 2026-06-17

- Open history menus from the Back and Forward controls to jump to an earlier or later page.
- Resize Picture-in-Picture from either top corner.
- Shared chat images render Markdown tables correctly.
- If a webpage intercepts ⌘S, a prompt tells you to press it again to toggle the sidebar.

### [1.0.40] - 2026-06-16

- Manually checking for updates now opens the Sparkle update prompt.

### [1.0.39] - 2026-06-15

- The Agent can suggest actions based on memory; they run only after you explicitly confirm them.
- Inspect Dao's stored memories through a SQL browsing interface.
- Keep encoded characters intact in DuckDuckGo result URLs.

### [1.0.38] - 2026-06-14

- Dream reports summarize browsing activity, and feedback about your habits can be saved for later reports.
- Agent conversations can use memories associated with the current site.
- Improve memory-storage compatibility and refresh localized Agent interfaces correctly.
- Match the address-bar background more closely to the surrounding interface and improve scrolling to new tabs.

### [1.0.36] - 2026-06-11

- The command bar learns from suggestions you accept, making repeated destinations easier to reach.
- Lighten the command-bar placeholder text.

### [1.0.35] - 2026-06-10

- Clear the sidebar scrollbar's hover highlight when the pointer leaves.
- Preserve split-view layout when closing the browser.
- Fix Markdown emphasis containing spaces in Agent messages.

### [1.0.34] - 2026-06-09

- Hide the fullscreen-exit bubble when using the sidebar layout.
- After picking a webpage element, focus moves to the Agent so you can continue typing.
- Increase command-bar text size.

### [1.0.33] - 2026-06-07

- The sidebar shows when an update is available. Checks run at startup and every 24 hours.
- Refine the colors of the Picture-in-Picture top bar.

### [1.0.32] - 2026-06-07

- Drop a tab at a specific position among pinned tabs.
- Fix unreliable extension-install buttons.
- Searches respect the default search engine selected in settings.

### [1.0.31] - 2026-06-06

- Pin frequently used tabs in the sidebar and arrange them with drag and drop.

### [1.0.30] - 2026-06-04

- Show the drop indicator correctly when a dragged tab returns to the sidebar.
- Refine cleanup of old release files.

### [1.0.29] - 2026-06-03

- New tabs appear in the correct position, and opening a tab no longer accepts history completion unintentionally.
- The first-run welcome page reuses the startup tab instead of opening an extra one.
- Packaging checks helper-app permissions, and maintainers can clean up old release files with a dedicated command.

### [1.0.28] - 2026-06-02

- Pick an element on a webpage and attach it to an Agent conversation.
- Fix overlapping rows in the downloads list.
- Fix a debug-startup crash when Manifest V2 extensions are enabled.

### [1.0.27] - 2026-06-01

- Show URL ports and fragments correctly, and refresh the address bar after navigation.
- Apply site-specific styles in webpage Picture-in-Picture and select the correct player area on Bilibili.

### [1.0.26] - 2026-05-31

- Add support for Manifest V2 extensions.
- Keep the active tab in view and correct where tabs opened from external links appear.
- Clear sidebar tooltips that remain visible after they should have closed.
- Improve images generated when sharing chats and refine system-dialog styling.

### [1.0.25] - 2026-05-28

- Disable individual skills from the skill manager without removing them.
- Refresh text in Chromium-provided parts of the interface.

### [1.0.24] - 2026-05-25

- Make address-bar icons easier to see and fix clicks when the sidebar is collapsed.
- Refine the sidebar toggle's hover feedback and remove obsolete entries from the update feed.

### [1.0.23] - 2026-05-24

- The website now lists past versions with their download links.
- Keep the full version history in the update feed, and improve version handling and rollback in the release script.

### [1.0.22] - 2026-05-23

- The downloads popup has a clearer file list and revised cancel buttons.

### [1.0.21] - 2026-05-23

- The command bar shows suggested completions as faint text while you type.
- Choose how old chat sessions are restored instead of always using the same recovery behavior.
- Add the macOS keychain permission needed for Touch ID and WebAuthn.
- Refine extension popups and navigation buttons, and improve download redirects and loading feedback on the website.

### [1.0.20] - 2026-05-21

- Agent requests can use same-origin cookies to read content that requires a website login.
- Changes to the skill list stay in sync across open Agent pages.
- Disable browser sync that is not yet supported and remove the retired reverse-engineering skill.
- Refine the appearance of inline code in chat messages.

### [1.0.19] - 2026-05-20

- This tag uses the same source commit as 1.0.18, with no additional code changes.

### [1.0.18] - 2026-05-20

- Page-loading progress is now visible in the browser interface.

### [1.0.17] - 2026-05-19

- Fix print preview for Dao pages and compatibility problems between Dao and Chromium addresses.
- Add usage-event statistics to help understand how browser features are used.
- Improve update-feed generation and incremental update uploads.

### [1.0.16] - 2026-05-19

- Command-bar suggestions are easier to read in both light and dark themes.
- QR-code recognition messages now follow the interface language.
- More Agent settings are localized, including model providers, sessions, memory, and skills.
- Translation jobs can run in parallel, and the website has a fuller introduction to Agent features.

### [1.0.15] - 2026-05-18

- The earliest available desktop tag is based on Chromium 147. The items below describe what was already available at that point.
- Keep tabs in a vertical sidebar and organize them into folders.
- View pages side by side with split view, and restore your browsing session when reopening the browser.
- Use the keyboard-driven command bar to enter addresses, search the web, and switch tabs.
- Open external links in Little Dao, a small browsing window.
- Manage extensions and downloads, share pages, and generate QR codes from the browser controls.
- Chat in the AI sidebar with streaming replies, and attach the current page or selected text as context.
- The Agent can search the web, use skills and long-term memory, operate the browser, and perform initial workspace file operations.
- Control playback from the sidebar or keep media in an enhanced Picture-in-Picture window.
- Use the interface in multiple languages and receive macOS updates through Sparkle.

## [android] Android

### [Unreleased] Unreleased

- Reconstruct desktop and Android release notes from Git tags, checking each version, adding feature details, and rewriting the explanations.

### [0.1.9] - 2026-10-07

- Speed up QR-code scanning.
- Keep the current tab visible when opening the tab grid.

### [0.1.8] - 2026-10-06

- Long-press an image to preview, copy, or download it; HTTP(S) images can also open in a background tab.
- Zoom and pan in image previews, and keep newly opened image tabs in the same regular or private browsing mode.
- Downloads use the browser's response stream, preserving signed-in requests and supporting page-generated files such as blob and data downloads.
- Show ongoing downloads in a foreground notification and report incomplete transfers as failures.
- Private-download records are not saved between app sessions.
- Ignore duplicate download retries and show an error instead of crashing when a retry fails.
- Reload a tab when Android has killed its webpage process.
- The Back action returns from library screens and closes the drawer correctly.
- Moving a bookmark to a list that already contains it no longer crashes; the destination entry is retained.
- Keep QR-scanner controls clear of the system bars.
- Include Indonesian and Norwegian translations in the app.

### [0.1.7] - 2026-10-06

- Editing the address selects the full URL, making it easier to replace.
- Tap anywhere in the expanded search field to bring the keyboard back.

### [0.1.6] - 2026-10-03

- Follow the system's light or dark theme and keep webpage theme preferences in sync.
- Keep the page viewport stable when the keyboard opens.

### [0.1.5] - 2026-10-01

- Keep the launcher icon dark when the device uses night mode.

### [0.1.4] - 2026-10-01

- Deleting a download now tells you whether its file was also removed.

### [0.1.3] - 2026-09-17

- Check for updates from Settings > About, read the release notes, and download the matching APK in the app.
- Automatic update checks run quietly during foreground use, at most once a day. They are enabled by default and can be turned off.
- See available-update hints and the last successful check time in settings.
- Follow download progress, cancel or retry an update download, and continue installation after granting the system permission.
- Verify the downloaded update's size, checksum, package, and version before opening the installer. Updates are not downloaded or installed without your action.
- Fix a startup crash related to font scaling.

### [0.1.2] - 2026-09-15

- Download an APK built for your device's CPU architecture instead of using one package for every architecture.

### [0.1.1] - 2026-09-12

- The earliest available Android tag is built on GeckoView and includes an address/search bar, a tab grid, and restoration of regular browsing sessions.
- Scan QR codes, save bookmarks and reading-list entries, and browse history.
- Use the bundled uBlock Origin, optionally enable KISS Translator, and install or manage extensions.
- Open web links from other apps, and confirm before following links into external apps.
- Manage downloads with transfer-speed information and open downloaded APKs through the system installer.
- Follow the system language and save browser preferences between launches.
- Publish signed APKs through tagged GitHub releases.

## [ios] iOS

### [Unreleased] Unreleased
