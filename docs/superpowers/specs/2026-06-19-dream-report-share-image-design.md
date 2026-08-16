# Dream Report Share Image Design

## Goal

Add a share affordance to Dream Report pages so users can click "Copy image"
and place a branded PNG of the current dream report on the system clipboard.

This feature must share the same implementation path as the existing Agent chat
"Copy as image" feature instead of introducing a DOM screenshot path or a second
canvas renderer.

## Confirmed Product Decisions

- The share image contains the dream report content only.
- The image includes a title, the report date, the report Markdown body, and a
  Dao Browser footer.
- The image does not include the history list, habit confirmation controls,
  habit feedback state, debug material, page buttons, scroll position, or any
  other interactive UI state.
- The entry point lives on the standalone Dream Report page
  (`dao://dream/` and `dao://dream/today`), not on the compact Agent morning card.
- Failure to copy an image shows an error state; the button does not fall back to
  copying text because its user-facing promise is image sharing.

## Existing Context

Dream Report UI is implemented in
`src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`.

Dream reports are already available to the WebUI as `DreamReportData`:

- `id`
- `dreamDate`
- `reportMarkdown`
- `habits`
- `debugMaterialJson`
- `triggerKind`

The current chat "Copy as image" implementation is in
`src/dao/browser/ui/webui/resources/agent/dao_share_image.ts`. It renders a
Markdown Q/A card to an `OffscreenCanvas`, converts it to a PNG `Blob`, and the
chat view writes that blob to the clipboard with `ClipboardItem`.

## Recommended Approach

Refactor `dao_share_image.ts` into shared rendering primitives plus separate
content templates:

1. A shared layer for Markdown lexing, block layout, theme selection, max-height
   clipping, overflow fade, footer painting, canvas creation, and PNG blob
   conversion.
2. The existing Q/A template, kept behind the existing `renderShareImage(ctx)`
   export so chat callers do not need to change their semantic API.
3. A new dream report template, exposed as `renderDreamReportShareImage(ctx)`,
   that uses the shared Markdown renderer without a user-question bubble.
4. A shared clipboard helper, exposed as `copyPngBlobToClipboard(blob)`, so chat
   and dream report image sharing use the same `ClipboardItem({'image/png':
   blob})` path and the same API-availability failure behavior.

This keeps the implementation shared where it matters while allowing each
surface to keep a content-specific layout and footer.

## Rejected Alternatives

### Reuse the Q/A Template Directly

The dream report body could be passed as the existing `answer` field and the
question bubble could be omitted. This has the smallest implementation diff, but
it leaks chat-specific semantics into a report feature. The footer would say
"Answered by Dao Browser" unless more conditionals were added, and future
maintenance would be confusing.

### Screenshot the Report DOM

A DOM screenshot would appear closer to the rendered page, but it would not share
the current "Copy as image" implementation. It would also be sensitive to
scrolling, Shadow DOM boundaries, WebUI CSP, font timing, and responsive layout.

## Dream Report Image Layout

The image should use the existing share canvas width and max-height behavior so
clipboard payload size remains bounded.

Content order:

1. Header title: localized `dream.page.title`.
2. Date line: localized `chat.dream.card_date` with `report.dreamDate`.
3. Body: `report.reportMarkdown`, rendered through the same Markdown block
   renderer used by the chat share image.
4. Footer: localized `dream.share.footer`, English default
   `Dreamed by Dao Browser`.

The template should use the same light/dark theme selection as the chat share
image. It should not draw the chat user bubble or the chat source line.

If the report content exceeds the maximum image height, use the existing fade
and ellipsis behavior.

## Dream Page Interaction

When `DaoDreamApp` has a current report, the page header shows a compact
`Copy image` button near the date. The button is hidden while loading, in error
state, or when no report is available.

Click flow:

1. Disable the button and show a transient busy state.
2. Call `renderDreamReportShareImage()` with:
   - `title`: `t('dream.page.title')`
   - `dateLabel`: `t('chat.dream.card_date', {date: report.dreamDate})`
   - `markdown`: `report.reportMarkdown`
   - `footer`: `t('dream.share.footer')`
3. Call `copyPngBlobToClipboard(blob)`.
4. On success, briefly show `Copied image`.
5. On failure, briefly show `Copy failed`.
6. Restore the normal button label.

No C++ bridge, database, or memory-store changes are required.

## Internationalization

Add English and Simplified Chinese entries only:

- `dream.page.copy_image`
- `dream.page.copy_image_copied`
- `dream.page.copy_image_failed`
- `dream.share.footer`

Do not run `i18n.sh`. Other locales fall back according to the existing i18n
runtime.

Suggested English text:

- `dream.page.copy_image`: `Copy image`
- `dream.page.copy_image_copied`: `Copied image`
- `dream.page.copy_image_failed`: `Copy failed`
- `dream.share.footer`: `Dreamed by Dao Browser`

Suggested Simplified Chinese text:

- `dream.page.copy_image`: `复制图片`
- `dream.page.copy_image_copied`: `已复制图片`
- `dream.page.copy_image_failed`: `复制失败`
- `dream.share.footer`: `Dao Browser 梦境生成`

## Error Handling

- No report loaded: hide the button.
- Clipboard API unavailable: show failure state.
- Canvas or blob conversion failure: show failure state.
- Markdown lexer failure: use the existing plain-text fallback in
  `dao_share_image.ts`.
- Long report: use existing max-height clipping and overflow fade.

## Privacy Boundary

The share image intentionally excludes:

- Habit candidates and evidence.
- Confirmed or rejected habit feedback state.
- Debug input material JSON.
- Material stats.
- Any browser history list or navigation UI.

The user still explicitly triggers the copy action, and only the generated PNG is
written to the system clipboard.

## Testing

### `dao_share_image.test.ts`

Add coverage for `renderDreamReportShareImage()`:

- It paints the dream title, date label, Markdown body, and dream footer.
- It does not paint the chat footer `Answered by Dao Browser`.
- It does not paint a user question bubble when the dream report template is
  used.
- It keeps Markdown rendering parity for headings, paragraphs, and lists via the
  shared block renderer.

Add coverage for `copyPngBlobToClipboard()`:

- It writes `image/png` through `ClipboardItem`.
- It rejects when `ClipboardItem` or `navigator.clipboard.write` is unavailable.

### `dao_dream_app.test.ts`

Add Dream page interaction coverage:

- A loaded report renders the `Copy image` button.
- Loading, error, and empty states do not render the button.
- Clicking the button calls the dream report image renderer and clipboard helper.
- Success shows `Copied image`.
- Failure shows `Copy failed`.
- The image context uses `report.reportMarkdown` and `report.dreamDate`, not
  habits or debug material.

## Verification

For implementation, run:

```bash
npm run test:webui
npm run lint:lit
```

No `npm run rebuild` is required unless implementation touches C++, GN files, or
Chromium integration patches.

## Out Of Scope

- Native share sheet integration.
- Downloading the PNG as a file.
- Public share links.
- Sharing the compact Agent dream card.
- Sharing debug material.
- Sharing habit feedback state.
