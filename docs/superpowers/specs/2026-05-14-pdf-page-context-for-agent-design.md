# PDF Page Context for Dao Agent — Design

**Date:** 2026-05-14
**Topic:** Allow Dao Agent to include PDF text as page context

## Problem

When a user opens a PDF in Dao (file:// drag-drop, http(s):// PDF URLs, etc.) and asks the agent about it, the captured "current webpage" attachment is essentially empty. Root cause:

- A PDF tab's top-level frame is the built-in PDF Extension (`chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/index.html`), not the PDF file itself.
- `dao_page_capture.ts` injects `Readability.parse()` via `Runtime.evaluate` on the top frame. The PDF Extension's DOM is a `<pdf-viewer>` custom element and a toolbar — Readability has no article to extract.
- Fallback to `document.body.innerText` only yields toolbar labels. The actual PDF text is rendered by PDFium inside a plugin and not accessible from JS in the extension frame.

## Goal

The agent's `<current-webpage>` block for a PDF tab should contain the PDF's textual content (up to a budget), so questions like "summarize this paper" or "what does page 3 say about X" work the same as on an HTML page.

## Non-Goals

- Not extracting PDF structure (headings, links, paragraphs). Plain text in reading order is the target. Structure extraction can come later if needed.
- Not handling password-protected, encrypted, or DRM-restricted PDFs beyond returning an empty payload that lets the agent gracefully degrade.
- Not extracting images, annotations, or form fields.
- Not pre-extracting / caching. Capture is on-demand when the agent asks for page context.

## Approach

Use the existing `pdf::PDFDocumentHelper` Chromium API (`components/pdf/browser/pdf_document_helper.h`) — the same channel screen readers use to read PDFs. It exposes:

- `MaybeGetForWebContents(contents)` — returns nullptr when the tab is not a PDF (also serves as our PDF detection)
- `IsDocumentLoadComplete()` / `RegisterForDocumentLoadComplete(closure)` — readiness signal
- `GetPdfBytes(size_limit, cb)` — callback receives `(status, bytes, page_count)`. Called with `size_limit=0` to cheaply obtain only the page count.
- `GetPageText(page_index, cb)` — returns the page's plain text as `std::u16string`

Why this API rather than PDFium C exports or the generic `WebContents` accessibility tree:

- It is the official browser-process surface for PDF text. No need to copy bytes back into the main process or enable a11y mode.
- Returns text in reading order as PDFium understands it (best signal available without parsing layout ourselves).
- Public API on a public Chromium component; no Chromium-source patch is required if the dao target can already link `//components/pdf/browser` (verified at build time).

## Components

### 1. Native handler `getPdfText` (C++)

Add a new handler method on `DaoAgentMemoryHandler` (or a new sibling handler if the existing one is already busy — decide during implementation). Registered alongside the other agent handlers in `dao_agent_ui.cc`.

**Input:** none (operates on the active tab via `EnsureAttached()`).

**Output (resolved JS callback payload):**

```json
{
  "isPdf": true,
  "url": "file:///path/foo.pdf",
  "title": "Sample.pdf",
  "pageCount": 12,
  "text": "...concatenated page text...",
  "truncated": false,
  "truncatedAtPage": 7        // present only when truncated
}
```

Or `{ "isPdf": false }` when the active tab is not a PDF. Or `{ "isPdf": true, "error": "..." }` on failure.

**Flow:**

1. `EnsureAttached()` → `WebContents* contents`. If null, return `{ isPdf: false }`.
2. `helper = pdf::PDFDocumentHelper::MaybeGetForWebContents(contents)`. If null, return `{ isPdf: false }`.
3. Capture `url = contents->GetVisibleURL()` and `title = contents->GetTitle()` now (page may navigate away by the time async chain completes — these are the values we report).
4. If `!helper->IsDocumentLoadComplete()`: queue a 5-second timeout that, if it fires, resolves with `{ isPdf: true, error: "PDF still loading" }`. Then call `helper->RegisterForDocumentLoadComplete(...)` and continue inside its closure.
5. Call `helper->GetPdfBytes(/*size_limit=*/0, cb)`. In `cb`:
   - If `status != kSuccess` or `page_count == 0`: resolve with `{ isPdf: true, error: "Failed to read PDF" }`.
   - Else kick off a sequential `GetPageText(0)`, `GetPageText(1)`, ... pipeline (next-page request issued only after previous result arrives, to avoid hammering the plugin).
6. Maintain accumulator: `std::string text`. After each page, append `text += "\n\n--- Page " + (i+1) + " ---\n\n" + utf8(page_text)`. The page-marker header gives the model a positional cue for "page N says X" questions.
7. After each append, if `text.size() >= 512 * 1024`: stop, set `truncated = true`, `truncated_at_page = i + 1`, append `"\n\n[... truncated. Total " + pageCount + " pages, captured first " + truncatedAtPage + " pages.]"`, and resolve.
8. After the loop completes naturally: resolve with the full text.

**Threading / lifetime:** weak-ptr the handler into each lambda; if the handler dies mid-fetch, drop the result quietly.

**Build wiring:** add `//components/pdf/browser` to `src/dao/browser/BUILD.gn` (or wherever the agent handler's source_set is defined). If visibility blocks, patch `components/pdf/browser/BUILD.gn` to add the dao source_set as an allowed dep — but try without a patch first.

### 2. Frontend `dao_page_capture.ts`

Modify `captureCurrentPageMarkdown()`:

```ts
export async function captureCurrentPageMarkdown(): Promise<PageCapture | null> {
  // PDF fast path: ask native if the active tab is a PDF and, if so,
  // use PDFium-extracted text instead of running Readability on the
  // PDF Extension's hollow DOM.
  try {
    const pdf = await callNative('getPdfText') as
        { isPdf?: boolean; url?: string; title?: string;
          pageCount?: number; text?: string; truncated?: boolean;
          error?: string } | null;
    if (pdf && pdf.isPdf && !pdf.error && pdf.text && pdf.url) {
      const header = `# ${pdf.title || 'PDF'} (PDF, ${pdf.pageCount ?? '?'} pages)\n\n`;
      return {
        url: pdf.url,
        title: pdf.title || '',
        markdown: header + pdf.text,
        fallback: false,
      };
    }
    // isPdf=false, or PDF but error: fall through to Readability path.
  } catch (_) {
    // Native handler missing or threw — fall through.
  }

  // ... existing Readability + Turndown path unchanged ...
}
```

`isCapturablePageUrl` is unchanged: PDF tabs surface a `file://` or `https://` URL at the top level, neither of which is rejected.

### 3. Wire `getPdfText` into the `callNative` bridge

`agent_bridge.ts` doesn't enumerate native handlers — it forwards any name to `cr.sendWithPromise`. The only requirement is that the handler is registered on the C++ side. No changes needed on the bridge.

## Data Flow

```
User opens foo.pdf → Dao loads PDF Extension → PDFium starts rendering
User opens command bar, Cmd+L, asks "summarize this"
  → dao_chat_view.ts calls captureCurrentPageMarkdown()
    → callNative('getPdfText')
      → C++ DaoAgentMemoryHandler::HandleGetPdfText
        → MaybeGetForWebContents → PDFDocumentHelper*
        → GetPdfBytes(0) → page_count
        → for i in 0..page_count: GetPageText(i) → append until 512 KiB
        → resolve { isPdf: true, ..., text: "..." }
    → buildPageAttachment({ url, title, markdown: header + text })
  → attachment posted with the user message
```

## Error Handling

| Failure | Native behavior | Frontend behavior |
|---|---|---|
| Active tab is HTML, not PDF | `{ isPdf: false }` | Fall through to Readability |
| `MaybeGetForWebContents` returns nullptr but URL ends in `.pdf` (PDF not yet hooked up) | `{ isPdf: false }` | Fall through to Readability (returns toolbar-only markdown — same as today; not worse) |
| PDF still loading after 5s | `{ isPdf: true, error: "PDF still loading" }` | Fall through to Readability |
| `GetPdfBytes` returns `kFailed` (encrypted, corrupt) | `{ isPdf: true, error: "Failed to read PDF" }` | Fall through to Readability |
| `GetPageText` returns empty for a page | Treated as legitimate empty page; `--- Page N ---` block still emitted with no body |
| User navigates away mid-fetch | Detect via `WebContentsObserver::PrimaryPageChanged` or compare `contents->GetVisibleURL()` against the URL captured in step 3 inside each `GetPageText` callback; on mismatch, resolve with `{ isPdf: true, error: "Navigation occurred during capture" }` so the promise doesn't hang | Fall through to Readability on the new page |
| PDF text exceeds 512 KiB | `{ truncated: true, truncatedAtPage: N }`, payload ends with truncation notice | Frontend uses payload as-is |

## Testing

**Manual smoke test (required for completion):**
1. Drag a multi-page PDF (e.g., a 10+ page research paper) from Finder into Dao.
2. Open command bar, ask "summarize this in 3 bullets".
3. Verify the assistant response references actual content from the PDF (not just the filename or "I can't see the document").
4. Repeat with an http(s):// PDF.
5. Repeat with a 200+ page PDF — verify the response acknowledges the content of the first ~7-10 pages and notes truncation (or behaves consistently without crashing).

**Browser test (next iteration, not blocking):** Add a case to `dao_browser_browsertest.cc` that:
1. Loads a test PDF fixture (Chromium has `pdf/test/data/` fixtures).
2. Calls `getPdfText` directly on the handler.
3. Asserts `isPdf == true`, `pageCount > 0`, `text` contains a known string from the fixture.

Defer until after manual verification; current SQLite-FTS5 issue (see memory) blocks some agent browser tests anyway.

## Open Questions

None blocking. Possible follow-ups, not part of this spec:

- Should PDF accessibility tree (paragraphs, headings, links) replace raw text for better model grounding? Decide after seeing real-world quality.
- Should page-marker headers be configurable or omitted on very short PDFs?
- Pre-extract on PDF load (the rejected timing option) — revisit if users hit perceptible latency on each agent query.

## Files Touched (summary)

- **New native handler**: `src/dao/browser/ui/webui/dao_agent_ui.cc` + `dao_agent_ui.h` — add `HandleGetPdfText` and registration.
- **Frontend**: `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts` — PDF fast path in `captureCurrentPageMarkdown`.
- **Build**: `src/dao/browser/BUILD.gn` (or whichever GN file defines the agent handler's source_set) — add `//components/pdf/browser` dep. May or may not require a `components/pdf/browser/BUILD.gn` visibility patch.
- **No Chromium source patches expected** beyond the optional visibility tweak.
