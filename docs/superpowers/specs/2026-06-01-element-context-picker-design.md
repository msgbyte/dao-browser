# Element Context Picker Design

## Goal

Let the user click a picker button in the Agent panel, select an element in the active WebContents, and reuse that selected element as context in later Agent turns.

## Scope

This is a session-scoped element context feature. It captures one selected element, shows it as a chip above the composer, and attaches an `<element-context>` block to subsequent messages until the user dismisses it.

## Existing Code Reused

- `dao_chat_view.ts` already renders page and text-selection chips above the composer.
- `dao_page_capture.ts` already owns page/selection capture helpers and attachment builders.
- `DaoAgentUIHandler::HandleExecuteScript` already lets WebUI inject JavaScript into the active tab.

## Architecture

The first version stays in the WebUI layer. `dao_page_capture.ts` injects a picker script into the active page through `executeScript`. The script installs hover and click capture listeners, highlights the hovered element, blocks the click that selects the element, and stores the captured element data on `window.__dao_element_picker__`. The WebUI polls that state until the element is selected, canceled, or timed out.

The selected element is converted immediately into a stable locator. The locator is not guaranteed to survive every site redesign, but it is reusable across normal same-site sessions because it stores semantic clues, stable attributes, CSS fallback, nearby text, and bounds.

```text
Agent picker button
  -> startElementPicker()
  -> executeScript installs page picker
  -> user hovers/clicks WebContents element
  -> page script captures locator and prevents the original click
  -> WebUI polling receives selected element
  -> DaoChatView stores pendingElementContext_
  -> later sends include buildElementContextAttachment()
```

## Not In Scope

- Cross-origin iframe element picking.
- Multi-element selection.
- Profile-persistent element contexts.
- Full script replay or `resolve_locator`.
- Native C++ overlay UI.

## Failure Handling

- Esc cancels the picker.
- Starting a new picker cancels any existing picker.
- Timeout cancels the picker and clears injected listeners.
- Picker click uses capture-phase `preventDefault()` and `stopPropagation()` to avoid triggering destructive page actions.
- If the active tab cannot run injected script, the picker returns no context and the Agent UI shows a localized failure toast.

