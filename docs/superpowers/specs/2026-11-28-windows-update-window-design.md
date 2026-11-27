# Windows update window

[Open the interactive design](windows-update-window/preview.html). It opens on the selected installation frame. The controls below the window belong to the design preview.

## Experience

Clicking the ready update should lead continuously from the old browser window, through installation, to the first visible window of the updated browser. The existing Windows installer host owns the progress window throughout that interval. Users already receive this helper with the update package.

The selected design uses a 368 × 268 DIP window with the Dao mark centered inside a softly illuminated blue ring. Only the ring rotates. A heading names the current operation and one line explains what happens next. The second visual option was chosen over the horizontal step tracker and the compact status strip. It keeps the installation frame recognizable while giving each phase a complete, readable message.

## Window lifecycle

| Trigger | Visible behavior |
| --- | --- |
| User selects the ready update | Immediately acknowledge the click in the browser. Validate the existing handoff and prepare the helper window before requesting browser shutdown. If preparation fails, keep the browser open and present the error there. |
| The helper is ready | Show the preparing frame on the browser's monitor, near its previous center. Keep browser leave-page and download confirmations in front. If the user cancels shutdown, dismiss the helper and return the update to its ready state. |
| Shutdown is committed, the original process has exited and the silent installer has started | Show the installation frame until the child exits. The current backend reports completion by exit code. Rotate the ring to show activity and use the heading for actual progress. |
| The installer succeeds and relaunch starts | Show the reopening frame. Keep it visible while the browser starts. Dismiss it when Windows shows the first regular window belonging to the updated browser. Session restoration can continue after that point. |
| The browser window takes more than 30 seconds to appear | Explain that the update is installed and the helper is waiting for the browser window. Keep observing while the user reads or minimizes the message. |
| Installation fails | Stop the ring. Offer **Retry installation** and **Log details**. Revalidate the cached installer and registered installation before retrying. Allow the user to dismiss the error. |
| Installation succeeds but relaunch fails | Offer **Open Dao** and **Log details**. Retry only the browser launch. If a launch is still in flight, check for a visible window or process exit before allowing a second launch. |

The user can minimize the window while work continues and restore it from the Windows taskbar. The browser disables the update action during the handoff. A relaunch should respect users working in another application.

## Implementation boundaries

- Extend the existing Win32 update mode in `scripts/windows-installer/native/update.h`. Keep the UI message loop responsive during pipe operations and installer waits. The update window should run with the resources shipped inside the update package, including on systems without WebView2.
- Keep the existing authenticated exit handoff and the installer exit code authoritative. Use Windows window visibility notifications and a bounded wait with a visible-window check against the registered browser. This also covers older clients and mixed-version upgrades. Surface a recovery action when readiness cannot be established.
- Match system appearance, text scaling and high contrast. Respect reduced motion with a stationary arc. Announce each changed heading once to assistive technology. Keep keyboard focus on any browser prompt that needs an answer.
- Use the Windows installer's English and Simplified Chinese localization sources for the native window. Error details should expose the installer and handoff log locations and the available failure code.

## Acceptance

Walk through a successful update, a cancelled leave-page prompt, minimization during installation, an installation error and a relaunch error. Confirm that the window reflects each real event, that only one installer runs, and that an already installed update can reopen the browser independently. Verify separately on a machine without WebView2 and with reduced motion enabled.
