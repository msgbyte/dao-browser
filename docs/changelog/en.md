# Changelog

## [desktop] Desktop

### [Unreleased] Unreleased

- Keep local agent approval dialogs from becoming too wide when tab titles or URLs are long.
- Search tabs by title or URL when choosing which tab a local agent may control.
- Keep macOS browser fullscreen active after a short Escape press dismisses the command bar, including when focus moves back to the page or another window.

### [1.0.125] - 2026-10-27

- Restore the virtual pointer and click ripple when AI clicks referenced page elements.
- Prevent overlapping AI clicks from being lost or timing out while the virtual pointer is moving.
- Keep earlier click ripples from hiding or interrupting the virtual pointer during consecutive AI clicks.
- Prevent AI clicks from timing out when switching to another AI-controlled tab.
- Hover a command bar recommendation and click ×, or press Shift+Delete on the selected recommendation, to remove it when deletion is supported, using the browser's built-in behavior.
- Keep AI task hover previews from reappearing after leaving the sidebar or covering a tab preview, and reduce unnecessary window redraws while watching a task.
- Show AI tasks above the New Tab button in the sidebar.
- Prevent browser slowdowns from repeated pinned-tab syncing when multiple windows or Dao Lite tasks are open.
- Close a Dao Lite task window after watching it without stopping its background work; reopen it anytime from the task list. Closing all browser windows or all windows of a profile still ends the affected tasks.
- Find MCP connections and local skills in the new Local agent collaboration section beside Dao Agent.
- Open Quick setup directly below the MCP switch and manage skill installation in a separate Skills and command card.
- Identify MCP server status more clearly, with a localized Ready indicator.
- Refresh local skill and command status when you return through the sidebar, scroll back to the section, or reopen the settings overview.
- Simplify local agent settings by removing the redundant Check for updates button.

### [1.0.124] - 2026-10-27

- Prevent native browser notifications from crashing when displayed or when their buttons are used.
- Restore keyboard access to notification buttons when switching focus between browser panes.
- Keep native browser notifications inside the active page in Split View.
- Keep loading indicators visible in native browser notifications after the Chromium update, including in incognito windows.
- Open the reading list from its notification without a crash or a blank panel.
- Browser-control requests now highlight who is asking, which page they want to use, and what access allows; expand details to see the full URL, version, and request time.
- Use the local MCP server and Dao Lite on Windows, with settings to install and manage local agent skills and the dao-browser command.
- Show Dao as `dao.exe` instead of `chrome.exe` in Windows Task Manager.
- Show copy-link confirmations and other native browser notifications in Dao's toast style inside the page area, with their existing buttons and menus.
- Hide Chromium's Google AI settings from the settings sidebar, overview, and search; Dao Agent settings remain available.
- Match the Dao Agent settings icon to its label color, including when hovered or selected.
- Keep address-bar status icons visible when the page URL is long.
- Browse release notes on the website in English, switch between desktop, Android, and iOS, and share links to individual versions.
- Find Changelog before the GitHub icon in the website navigation.

### [1.0.123] - 2026-10-25

- Update the browser engine to Chromium 155.0.8059.86.
- Keep recorded browsing time available in Dream reports for an ongoing period that spans midnight.
- Keep the selected address suggestion when its displayed text updates, instead of reverting to the original query.
- Show the image-copy confirmation when copying an image from the reading side panel.

### [1.0.122] - 2026-10-24

- Fix a macOS crash when a trackpad Back gesture closes the current tab and returns to the tab that opened it.
- Browse release notes on the website in English or Simplified Chinese, switch between desktop, Android, and iOS, and share links to individual versions.
- Restore the missing Dao Agent icon in the settings sidebar.
- Choose how long tabs stay inactive before moving to the stale folder with one-click options from 1 day to 2 weeks, or enter custom hours.
- Tab categories in settings now appear as compact chips: click a name to rename it, or use × to delete it.

### [1.0.121] - 2026-10-24

- Allow a local agent to use Dao Lite background tasks for one hour across reconnects, without repeated prompts or extending the time as it works; access to your own tabs still needs separate approval.
- Use the dao-lite skill for background searches and web tasks, and dao-browser when you explicitly want an agent to work on your existing tabs.
- Manage local agent skills and the dao-browser command manually in settings, with install, uninstall, and update controls and a notice when an installed copy needs updating.
- Codex now uses the shared skill in ~/.agents; updating or uninstalling it removes the duplicate copy previously installed by Dao in ~/.codex.

### [1.0.120] - 2026-10-24

- Show the address-bar AI control icon only when the current page is under agent control.
- Keep sidebar media controls steady during pause and mute, and prevent brief notification sounds from flashing or replacing the current media card.

### [1.0.119] - 2026-10-24

- Use an opaque main window on macOS so the desktop does not show through when browser content is not drawn.
- Windows installations using the old update address need one manual upgrade to continue receiving automatic updates.
- AI agents can now work in background task windows that use your sign-ins without taking over your tabs. The sidebar lists each task with its progress; hover to see it live, open it to watch the page and steps, take over, hand it back, or stop it.
- Background AI tasks pause and let you know when they need you, such as signing in or answering a site's permission request, and continue after you hand them back.
- Background AI tasks also hand over to you when a site asks you to pick a file or a device, or shows a sign-in, security key or certificate prompt, and they can answer a page's simple confirmation dialogs.
- When an AI task needs you, Dao also sends a system notification that opens the task.
- Files that background AI tasks download are saved without a prompt into a folder for each task in your Downloads.
- Finished AI tasks can move their result pages into an "AI results" folder in your sidebar, with a short notice of what was added, and finished tasks stay listed after Dao restarts.
- Dao Agent asks before it starts its first background task, and you can let it skip the question in AI settings.
- AI tasks can work in a clean window that does not use your sign-ins.
- Local agents such as Claude Code and Codex can use Dao through the new `dao-browser` command, which runs a whole multi-step browser script in one go; turning on the MCP server installs the command and a skill for these agents that stays current as Dao updates, and the settings show which agents are set up.
- Local agents can ask to work in background task windows only, and you can always allow a trusted agent to do so; turning off the MCP server takes that permission back.

### [1.0.118] - 2026-10-23

- Keep Windows update offers aligned with published releases when a problematic version is withdrawn.
- Fix a browser crash shortly after startup or when opening the AI sidebar.

### [1.0.117] - 2026-10-23

- MCP window approval now permits new background tabs without exposing your existing pages; grant existing pages separately and revoke individual pages without closing them.
- Reject ambiguous or changed page identities during MCP approval to prevent granting access to a different page.
- Keep the MCP permissions popup open during tool calls and show the latest tool for each connection.
- Manage each MCP connection and revoke access to individual pages from Settings.
- Keep browser-control approval dialogs compact when tab URLs or other details are long, with full text available on hover.

### [1.0.116] - 2026-10-22

- Prevent AI browser tools from timing out on a matching network response when optional result or error fields are absent.
- Reduce MCP screenshot response size by sending the image only once while keeping its format metadata.
- Give AI browser tools clearer guidance for reusable page scripts and correcting syntax errors before retrying.
- Fix links in Agent replies doing nothing when clicked; web links now open in a new tab in the main browser window while keeping the conversation open.

### [1.0.115] - 2026-10-21

- Move Close Tab to the bottom of the sidebar tab context menu, below Close Multiple Tabs.
- Upgrade the browsing engine to Chromium 154.0.8037.100 for updated website compatibility and security fixes.
- Fix pressing Enter before command bar suggestions appear opening AI settings instead of searching.
- Redesign Control Center with rounded tiles: extensions get their own section whose Add and Manage buttons stay visible however many extensions you install, and Dark Mode and Security now show their current status at a glance.

### [1.0.114] - 2026-10-18

- Open webpage links in new tabs immediately below their source tab, including background tabs, while keeping other new tabs at the top by default.
- Enable High Performance Mode for an individual tab from Control Center > More, directly below Share on macOS, to keep the page and its embedded content running in the background, prevent browser-initiated discarding and freezing, and bypass Energy Saver's frame limit without changing normal capture throttling; a checkmark and address-bar lightning icon update immediately, clicking the icon explains the protection and resource usage with a quick turn-off button, and a divider separates cache and cookie cleanup.
- Add browser regression checks for the High Performance Mode popup to catch incorrect tab state when dismissing the popup, switching tabs, or turning the mode off, including macOS mouse-click ordering.

### [1.0.113] - 2026-10-18

- Shorten the sidebar tab menu by grouping organization and bulk-close actions into submenus, while keeping common actions and their shortcuts directly accessible.
- Tabs opened by extensions such as Bitwarden or from the bookmark manager now appear at the top below pinned tabs by default, while explicitly requested positions are preserved.
- Building the Windows installer no longer prints missing-field-initializer warnings for process and startup structures.
- Clicking the Mini Dao address bar no longer leaves its background stuck in the pressed color after opening or dismissing the command bar.

### [1.0.112] - 2026-10-14

- Request MCP access to one specific tab by ID, including a background tab in another window; the approval dialog identifies it, and MCP tools on that connection cannot target other tabs or use open_tab.
- Avoid a browser crash when MCP is disabled while a browser tool is starting; pending calls stop as soon as their connection is destroyed.
- Remove redundant type assertions in tab-category settings tests that blocked the desktop browser-test build.

### [1.0.111] - 2026-10-13

- Fast Checks now installs website dependencies so download-page tests can run on clean CI runners.
- Release tests now use a local calendar date so changelog checks pass in both UTC CI and developer time zones.
- Windows build-argument tests use their own version fixture so normal release version bumps no longer break CI.
- Set a custom model ID for the experimental Jev plugin in Agent settings; leave it blank to keep using `jev-latest` for Agent and MCP browser tasks.
- Desktop releases no longer download installers and delta files again to verify uploads, avoiding failures from stale public 404 responses after successful R2 uploads.
- Release prechecks request uncompressed file sizes and tolerate missing size headers; network failures report the failed URL and underlying error.
- The website starts downloads automatically only on macOS; other devices show a manual download page with the configured platform installers instead of downloading a potentially incompatible file.
- Windows uploads to GitHub Releases now include only the installer, omitting separate checksum and update metadata files while retaining release validation and automatic updates through R2.
- Classify ungrouped tabs in the current window into folders with your configured AI model, or classify only the tabs below a chosen tab without disturbing existing groups.
- Reuse existing tab categories in each AI classification, create new categories when needed, and add, rename, or delete categories in Settings without closing their tabs.
- Closing tabs while AI classification is running no longer discards results for the remaining tabs, including when the closed tab was outside the selected range.
- AI classification can save results when older folder files contain duplicate references, without mistaking unchanged data for a conflicting edit.
- Fix a TypeScript build failure in AI tab classification and safely ignore cancellation events without a request ID when no request is running.

### [1.0.110] - 2026-10-12

- Build and upload macOS and Windows installers together with one desktop version through `npm run release`; use `release:mac` for a Mac-only release.
- Windows installations can download signed updates in the background, show update status in Settings About, and install from the sidebar after a cancellable restart while preserving browsing data.
- Publish Windows update metadata only after both installers are verified online; uncertain feed publication keeps the candidate version for a safe retry, and GitHub archives both installers.
- Sidebar tab close buttons no longer receive focus through Tab navigation or mouse clicks, preventing lingering focus outlines and accidental Space/Enter activation; close-tab shortcuts and context-menu actions remain available.
- Release documentation now points to one set of versioned histories, so maintainers no longer need to keep duplicate summaries.
- GitHub release notes now include individual commits, so changes made without a pull request are included in the update history.
- Keep readable release notes in English and Chinese for every change, with separate histories for desktop, Android, and iOS.
- Translate release notes into other languages with the existing translation script, reusing unchanged text even after a release is archived.
- Desktop releases archive pending notes under the new version and restore them if the release fails. Archived notes no longer prevent Windows from using the same release tag.
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

- Fixed website upload buttons not opening the file picker.
- Switch between regular and private tabs from the tab overview, where private browsing now has its own dark blue-gray space, and close all private tabs at once while keeping your regular tabs.

### [0.1.12] - 2026-10-18

- Webpage downloads now ask for confirmation before saving, with a prominent file name, available size, and a clearly marked Download button. Cancel or dismiss the dialog to leave no download task or file.
- Fixed some downloads being named `down` instead of the server-provided file name when extra response-header parameters appeared before the file name.

### [0.1.11] - 2026-10-14

- Enlarge the QR scanner guide so codes are easier to frame from both the new-tab page and browser menu.
- Fixed the keyboard failing to appear when editing a page address after touching the page content.

### [0.1.10] - 2026-10-11

- A failed Android version update or rejected release commit now restores the version and release notes together without overwriting concurrent edits; tag and push failures retain the committed release for retry.
- Set Dao as the default browser from a standalone card at the top of Android Settings. The card hides while Dao is the default and refreshes when returning from system settings.
- Android releases archive pending notes alongside the version update in the same commit.
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

- Long-press an image, including one inside a link, to download, copy, or preview it with pinch-to-zoom; web images can also open in a background tab.
- Webpage downloads now show the file name, size when known, and a clear Download button before saving; cancelling, dismissing, or closing the tab leaves nothing in Downloads.
- See how much of a running download has transferred and its current speed.
- Deleting a download now tells you whether its file was also removed.
- Pages that iOS stopped to free memory now reload by themselves instead of showing an error, without resending forms you submitted.
- Reopening Dao on a page you reached by submitting a form no longer sends the form again.
- The tab grid opens on the current tab and keeps it in view after closing other tabs.
- Editing the address selects the whole URL so typing replaces it, and tapping the search icon brings the keyboard back.
- The QR scanner shows a square guide to help frame codes.
