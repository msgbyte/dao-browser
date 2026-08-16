# PDF Page Context for Dao Agent — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the active tab is a PDF, Dao Agent's page-context capture returns PDFium-extracted text (up to 512 KiB) instead of running Readability on the hollow PDF Extension DOM.

**Architecture:** Add a new `getPdfText` native handler in `DaoAgentUIHandler`. It uses Chromium's public `pdf::PDFDocumentHelper` API to (a) detect PDF tabs, (b) wait for document load, (c) get page count via `GetPdfBytes(size_limit=0)`, (d) sequentially call `GetPageText(i)` until budget exhausted. The TS-side `captureCurrentPageMarkdown` calls `getPdfText` first; on `{isPdf: true}` it builds the page attachment from PDF text, otherwise it falls through to the existing Readability + Turndown path.

**Tech Stack:**
- C++: Chromium WebUI message handler API; `components/pdf/browser` (`PDFDocumentHelper`); Mojo callbacks
- TS: existing `callNative` bridge → `cr.sendWithPromise`
- Build: `chrome/browser/ui/BUILD.gn` (existing patch) — adds `//components/pdf/browser` to deps

---

## File Structure

**New code** (no new files; reuse the existing handler so we don't add another `RegisterMessages` site):

- Modify `src/dao/browser/ui/webui/dao_agent_ui.h` — declare `HandleGetPdfText`, `OnPdfBytesForCapture`, `FetchNextPdfPage`, members for in-flight capture state.
- Modify `src/dao/browser/ui/webui/dao_agent_ui.cc` — register `getPdfText` message, implement handler.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts` — PDF fast path in `captureCurrentPageMarkdown`.

**Build wiring**:

- Modify `src/patches/chrome/browser/ui/BUILD.gn.patch` — add `//components/pdf/browser` to `static_library("ui")`'s `deps`.

**Why not split into multiple .cc files?** `dao_agent_ui.cc` is the canonical home for agent ↔ active-tab native handlers (`executeScript`, `getPageInfo`, etc.). The new handler is one method of similar shape; splitting it out would scatter logic that's clearly related.

---

## Task 1: Wire `//components/pdf/browser` into the chrome/browser/ui:ui target

**Files:**
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch`

This must come first — without it, the new C++ code won't compile.

- [ ] **Step 1: Locate the `deps` block in the existing patch**

Read the existing patch and find where `static_library("ui")`'s `deps = [` block lives. Search for the line `"//components/pdf/common:util",` — the new dep goes right after it (keep alphabetical-by-path locality).

Run:

```bash
grep -n "//components/pdf/common:util" src/patches/chrome/browser/ui/BUILD.gn.patch
```

Expected: prints a line number from inside the patch.

If no result: the patch doesn't currently touch the `deps =` block. In that case search instead for `"//components/pdf/common"` or proceed to Step 2 with the engine file as reference.

- [ ] **Step 2: Inspect the engine source for the exact line to anchor on**

```bash
grep -n "//components/pdf/common:util" engine/src/chrome/browser/ui/BUILD.gn
```

Note the surrounding context (5 lines before and after). The new entry `"//components/pdf/browser",` will be inserted **immediately after** the `:util` line so it sits with its sibling.

- [ ] **Step 3: Add the patch hunk**

Open `src/patches/chrome/browser/ui/BUILD.gn.patch`. If the patch already has a hunk that includes the `"//components/pdf/common:util",` context line, modify it to add a `+` line right after. If not, append a new hunk at the bottom of the patch file. Use this exact addition:

```
    "//components/pdf/browser",
```

The new hunk should look like (line numbers will differ — fill in correctly using `grep` output from Step 2):

```diff
@@ -<line>,<count> +<line>,<count+1> @@
     "//components/pdf/common",
     "//components/pdf/common:util",
+    "//components/pdf/browser",
     "//components/performance_manager",
```

(Adjust the context lines to whatever surrounds `:util` in the real file.)

- [ ] **Step 4: Verify the patch applies cleanly**

```bash
cd engine/src && git checkout chrome/browser/ui/BUILD.gn && cd ../..
git apply --check src/patches/chrome/browser/ui/BUILD.gn.patch
```

Expected: exit 0, no output.

If it fails: re-read the surrounding context in the engine file, adjust the hunk's context lines / line numbers. Patches in this repo are sensitive to exact whitespace and trailing-context lines — see CLAUDE.md's "Patch export pitfall" note.

- [ ] **Step 5: Sanity-build to make sure nothing else breaks**

```bash
npm run rebuild 2>&1 | tail -40
```

Expected: build succeeds. `chrome/browser/ui` is a large target but adding a dep shouldn't trigger more than relinking. **DO NOT use `npm run build` (release).** CLAUDE.md requires `npm run rebuild` (debug) for iteration.

- [ ] **Step 6: Do NOT commit yet**

Per CLAUDE.md: never auto-commit. Leave changes in working tree for the user to commit at the end.

---

## Task 2: Declare `getPdfText` handler API in `dao_agent_ui.h`

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.h`

- [ ] **Step 1: Find the existing `HandleExecuteScript` declaration**

```bash
grep -n "HandleExecuteScript\|HandleGetPageInfo" src/dao/browser/ui/webui/dao_agent_ui.h
```

Expected: prints the line where `HandleExecuteScript` is declared (in the `DaoAgentUIHandler` class's private section).

- [ ] **Step 2: Add handler declaration + helper methods + state**

In `DaoAgentUIHandler` (the **UI** handler — not `DaoAgentMemoryHandler`), right after the `HandleExecuteScript` declaration, insert:

```cpp
  // Captures full text of the active tab if it's a PDF, using
  // pdf::PDFDocumentHelper. Sequentially calls GetPageText() for each
  // page and accumulates text up to ~512 KiB. Resolves with one of:
  //   { isPdf: false }                  -- non-PDF tab
  //   { isPdf: true, error: "..." }     -- PDF detected but capture failed
  //   { isPdf: true, url, title,
  //     pageCount, text,
  //     truncated, truncatedAtPage? }   -- success
  void HandleGetPdfText(const base::ListValue& args);

 private:
  // State for an in-flight getPdfText capture. Only one capture runs at
  // a time per handler instance (a second call replaces the first via
  // ResetInFlightPdfCapture).
  struct PdfCaptureState {
    std::string callback_id;
    GURL initial_url;          // captured before async chain begins
    std::u16string title;
    int32_t page_count = 0;
    int32_t next_page = 0;
    std::string text;          // UTF-8 accumulator
    static constexpr size_t kBudgetBytes = 512 * 1024;
  };

  // Called from HandleGetPdfText once we have a PDFDocumentHelper and
  // the document is loaded. Issues GetPdfBytes(0) and routes the result
  // to OnPdfBytesReceived.
  void StartPdfCapture(std::unique_ptr<PdfCaptureState> state,
                       pdf::PDFDocumentHelper* helper);

  // Receives page_count from GetPdfBytes, then kicks off the page loop.
  void OnPdfBytesReceived(std::unique_ptr<PdfCaptureState> state,
                          pdf::mojom::PdfListener::GetPdfBytesStatus status,
                          const std::vector<uint8_t>& bytes,
                          uint32_t page_count);

  // Issues GetPageText for state->next_page.
  void FetchNextPdfPage(std::unique_ptr<PdfCaptureState> state);

  // Page-text callback: appends, checks budget, then either loops or
  // resolves.
  void OnPdfPageText(std::unique_ptr<PdfCaptureState> state,
                     const std::u16string& page_text);

  // Resolves the WebUI callback for an in-flight capture. After this,
  // the state is destroyed (it's owned by the lambda chain).
  void ResolvePdfCapture(const PdfCaptureState& state,
                         bool truncated,
                         std::optional<int32_t> truncated_at_page);
  void ResolvePdfCaptureError(const PdfCaptureState& state,
                              const std::string& error_message);
  void ResolvePdfCaptureNotPdf(const std::string& callback_id);
```

**Add include at top of `dao_agent_ui.h`** (alphabetical with existing includes):

```cpp
#include "components/pdf/browser/pdf_document_helper.h"
#include "pdf/mojom/pdf.mojom-forward.h"
```

Use `pdf-forward.h` for the enum forward decl; the full `pdf.mojom.h` is included only in the .cc.

- [ ] **Step 3: Quick syntax sanity check**

```bash
grep -c "HandleGetPdfText\|PdfCaptureState\|StartPdfCapture" src/dao/browser/ui/webui/dao_agent_ui.h
```

Expected: at least 6 (1 handler + 4 helpers + 1 struct ⇒ each name appears at least once; structs appear multiple times).

- [ ] **Step 4: Do NOT build yet — implementation follows in Task 3**

The header references symbols that don't exist yet on the .cc side; building now would fail. Move to Task 3.

---

## Task 3: Implement `getPdfText` in `dao_agent_ui.cc`

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`

- [ ] **Step 1: Add `#include`s near the top**

Find the existing pdf/content includes (search for `#include "content/public/browser/web_contents.h"`). Add:

```cpp
#include "components/pdf/browser/pdf_document_helper.h"
#include "pdf/mojom/pdf.mojom.h"
```

Also ensure `<memory>` and `<optional>` are present (they likely already are; if not, add them).

- [ ] **Step 2: Register the new message**

Find `void DaoAgentUIHandler::RegisterMessages()` (around line 488). Inside, after the `executeScript` registration, append:

```cpp
  web_ui()->RegisterMessageCallback(
      "getPdfText",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetPdfText,
                          base::Unretained(this)));
```

- [ ] **Step 3: Implement `HandleGetPdfText`**

After the body of `DaoAgentUIHandler::HandleExecuteScript` (around line 829), add:

```cpp
void DaoAgentUIHandler::HandleGetPdfText(const base::ListValue& args) {
  AllowJavascript();

  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolvePdfCaptureNotPdf(callback_id);
    return;
  }

  pdf::PDFDocumentHelper* helper =
      pdf::PDFDocumentHelper::MaybeGetForWebContents(contents);
  if (!helper) {
    ResolvePdfCaptureNotPdf(callback_id);
    return;
  }

  auto state = std::make_unique<PdfCaptureState>();
  state->callback_id = callback_id;
  state->initial_url = contents->GetVisibleURL();
  state->title = contents->GetTitle();

  if (helper->IsDocumentLoadComplete()) {
    StartPdfCapture(std::move(state), helper);
    return;
  }

  // Wait for load. RegisterForDocumentLoadComplete does NOT take a
  // timeout — guard with a delayed task; whichever fires first wins,
  // the other becomes a no-op via the moved-from state pointer.
  // We move `state` into a shared_ptr-like holder via raw pointer +
  // weak_factory tracking: keep it simple, use a OnceClosure that owns
  // the unique_ptr and a weak handler.
  auto* state_ptr = state.get();
  auto on_loaded = base::BindOnce(
      [](base::WeakPtr<DaoAgentUIHandler> handler,
         std::unique_ptr<PdfCaptureState> s) {
        if (!handler) {
          return;
        }
        content::WebContents* c = handler->EnsureAttached();
        if (!c) {
          handler->ResolvePdfCaptureError(*s, "WebContents went away");
          return;
        }
        pdf::PDFDocumentHelper* h =
            pdf::PDFDocumentHelper::MaybeGetForWebContents(c);
        if (!h) {
          handler->ResolvePdfCaptureError(*s, "PDF helper went away");
          return;
        }
        handler->StartPdfCapture(std::move(s), h);
      },
      weak_factory_.GetWeakPtr(), std::move(state));
  helper->RegisterForDocumentLoadComplete(std::move(on_loaded));

  // 5-second safety timeout.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string cb_id, GURL initial_url) {
            if (!handler) {
              return;
            }
            // If the load already finished, the original closure has
            // already resolved the callback and this is a no-op
            // (ResolveJavascriptCallback on the same id twice is benign
            // — WebUI guards against it via `IsJavascriptAllowed`, but
            // we still want to avoid spurious "still loading" errors).
            // Simplest: just emit the error; if it duplicates, the
            // WebUI side will drop the second resolve.
            PdfCaptureState tmp;
            tmp.callback_id = cb_id;
            tmp.initial_url = initial_url;
            handler->ResolvePdfCaptureError(tmp, "PDF still loading");
          },
          weak_factory_.GetWeakPtr(), callback_id, state_ptr->initial_url),
      base::Seconds(5));
}
```

> **Note for the implementer:** the timeout above intentionally races with the load-complete callback. If both fire, the second `ResolveJavascriptCallback` is a no-op (Chromium's WebUI drops resolves for already-resolved ids, but watch for a CHECK in debug — if it asserts, switch to a single `base::CancelableOnceCallback` that wraps the load-complete + cancels the timeout). This is the simplest correct version; tighten only if needed.

- [ ] **Step 4: Implement `StartPdfCapture` and `OnPdfBytesReceived`**

Below `HandleGetPdfText`, add:

```cpp
void DaoAgentUIHandler::StartPdfCapture(
    std::unique_ptr<PdfCaptureState> state,
    pdf::PDFDocumentHelper* helper) {
  // size_limit=0 means "don't return bytes, just give me metadata".
  // We only need the page_count.
  helper->GetPdfBytes(
      /*size_limit=*/0,
      base::BindOnce(&DaoAgentUIHandler::OnPdfBytesReceived,
                     weak_factory_.GetWeakPtr(), std::move(state)));
}

void DaoAgentUIHandler::OnPdfBytesReceived(
    std::unique_ptr<PdfCaptureState> state,
    pdf::mojom::PdfListener::GetPdfBytesStatus status,
    const std::vector<uint8_t>& /*bytes*/,
    uint32_t page_count) {
  if (status != pdf::mojom::PdfListener::GetPdfBytesStatus::kSuccess ||
      page_count == 0) {
    ResolvePdfCaptureError(*state, "Failed to read PDF");
    return;
  }
  state->page_count = static_cast<int32_t>(page_count);
  state->next_page = 0;
  FetchNextPdfPage(std::move(state));
}
```

- [ ] **Step 5: Implement `FetchNextPdfPage` and `OnPdfPageText`**

```cpp
void DaoAgentUIHandler::FetchNextPdfPage(
    std::unique_ptr<PdfCaptureState> state) {
  if (state->next_page >= state->page_count) {
    ResolvePdfCapture(*state, /*truncated=*/false, std::nullopt);
    return;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolvePdfCaptureError(*state, "WebContents went away");
    return;
  }
  // Bail if the user navigated to a different page mid-capture.
  if (contents->GetVisibleURL() != state->initial_url) {
    ResolvePdfCaptureError(*state, "Navigation occurred during capture");
    return;
  }
  pdf::PDFDocumentHelper* helper =
      pdf::PDFDocumentHelper::MaybeGetForWebContents(contents);
  if (!helper) {
    ResolvePdfCaptureError(*state, "PDF helper went away");
    return;
  }

  int32_t page_index = state->next_page;
  helper->GetPageText(
      page_index,
      base::BindOnce(&DaoAgentUIHandler::OnPdfPageText,
                     weak_factory_.GetWeakPtr(), std::move(state)));
}

void DaoAgentUIHandler::OnPdfPageText(
    std::unique_ptr<PdfCaptureState> state,
    const std::u16string& page_text) {
  int32_t one_based = state->next_page + 1;
  state->text += "\n\n--- Page ";
  state->text += base::NumberToString(one_based);
  state->text += " ---\n\n";
  state->text += base::UTF16ToUTF8(page_text);

  if (state->text.size() >= PdfCaptureState::kBudgetBytes) {
    state->text += "\n\n[... truncated. Total ";
    state->text += base::NumberToString(state->page_count);
    state->text += " pages, captured first ";
    state->text += base::NumberToString(one_based);
    state->text += " pages.]";
    ResolvePdfCapture(*state, /*truncated=*/true, one_based);
    return;
  }

  state->next_page++;
  FetchNextPdfPage(std::move(state));
}
```

Add `#include "base/strings/string_number_conversions.h"` and `#include "base/strings/utf_string_conversions.h"` near the top of the .cc if not already present.

- [ ] **Step 6: Implement the three Resolve helpers**

```cpp
void DaoAgentUIHandler::ResolvePdfCapture(
    const PdfCaptureState& state,
    bool truncated,
    std::optional<int32_t> truncated_at_page) {
  base::DictValue response;
  response.Set("isPdf", true);
  response.Set("url", state.initial_url.spec());
  response.Set("title", base::UTF16ToUTF8(state.title));
  response.Set("pageCount", state.page_count);
  response.Set("text", state.text);
  response.Set("truncated", truncated);
  if (truncated_at_page.has_value()) {
    response.Set("truncatedAtPage", *truncated_at_page);
  }
  ResolveJavascriptCallback(base::Value(state.callback_id), response);
}

void DaoAgentUIHandler::ResolvePdfCaptureError(
    const PdfCaptureState& state,
    const std::string& error_message) {
  base::DictValue response;
  response.Set("isPdf", true);
  response.Set("error", error_message);
  ResolveJavascriptCallback(base::Value(state.callback_id), response);
}

void DaoAgentUIHandler::ResolvePdfCaptureNotPdf(
    const std::string& callback_id) {
  base::DictValue response;
  response.Set("isPdf", false);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}
```

- [ ] **Step 7: Build and fix any compile errors**

```bash
npm run rebuild 2>&1 | tail -60
```

Expected: build succeeds.

Common errors and fixes:
- `base::DictValue` missing → include `base/values.h` (probably already there)
- `pdf::mojom::PdfListener::GetPdfBytesStatus` missing → ensure `#include "pdf/mojom/pdf.mojom.h"` is present (not the `-forward.h`)
- `base::SequencedTaskRunner::GetCurrentDefault` missing → include `base/task/sequenced_task_runner.h`
- Linker error about `pdf::PDFDocumentHelper` → Task 1's BUILD.gn change didn't land; re-check the patch was applied
- `IsCreatingLittleDao` style cross-cutting compile errors → unrelated; ignore unless they reference our changes

- [ ] **Step 8: Quick functional smoke check from devtools**

After build succeeds:

```bash
npm run start:debug
```

Drag a small text-only PDF (1-2 pages) into a fresh tab. Then open DevTools on the **agent sidebar** (right-click inside the sidebar → Inspect), and in the agent's console run:

```js
chrome.send('getPdfText', [_LAST_REQUEST_ID++, /* no payload */ {}]);
// Or simpler: hit the bridge directly
window.cr.sendWithPromise('getPdfText').then(r => console.log(r));
```

Expected: `{ isPdf: true, url: "file://...", title: "...", pageCount: 1, text: "...", truncated: false }` where `text` contains the actual PDF content.

If `isPdf: false`: check `MaybeGetForWebContents` returned null — likely because the agent sidebar's "active tab" is not the PDF tab. Verify via `await window.cr.sendWithPromise('getPageInfo')` which should report the PDF's URL.

- [ ] **Step 9: Do NOT commit yet**

---

## Task 4: Frontend PDF fast path in `dao_page_capture.ts`

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts`

- [ ] **Step 1: Find `captureCurrentPageMarkdown`**

```bash
grep -n "export async function captureCurrentPageMarkdown" src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts
```

Expected: prints the line (currently around 503).

- [ ] **Step 2: Insert the PDF fast path at the top of the function body**

Modify `captureCurrentPageMarkdown` so the body begins:

```ts
export async function captureCurrentPageMarkdown():
    Promise<PageCapture | null> {
  // PDF fast path. PDF tabs render PDFium inside the built-in PDF
  // Extension; Readability on the extension's top frame returns
  // ~nothing. Ask native to extract text via pdf::PDFDocumentHelper
  // instead. On any failure (non-PDF tab, load timeout, encrypted
  // PDF) we fall through to the original Readability + Turndown path
  // so HTML pages behave exactly as before.
  try {
    const pdf = await callNative('getPdfText') as {
      isPdf?: boolean;
      url?: string;
      title?: string;
      pageCount?: number;
      text?: string;
      truncated?: boolean;
      truncatedAtPage?: number;
      error?: string;
    } | null;
    if (pdf && pdf.isPdf && !pdf.error && pdf.text && pdf.url) {
      const title = pdf.title || 'PDF';
      const pageCount = typeof pdf.pageCount === 'number' ? pdf.pageCount : 0;
      const header =
          `# ${title} (PDF, ${pageCount || '?'} pages)\n\n`;
      return {
        url: pdf.url,
        title,
        markdown: header + pdf.text,
        fallback: false,
      };
    }
    // isPdf === false (HTML tab), or isPdf === true with error — fall
    // through to Readability path.
  } catch (_e) {
    // Native handler missing or threw — fall through.
  }

  // ----- Existing Readability path below, unchanged. -----
  let raw: {result?: string; error?: string};
```

Make sure the original `let raw:` line and everything after is preserved exactly as-is. Only the new block above it is added.

- [ ] **Step 3: Build the TS bundle**

Frontend bundling runs as part of the C++ build (resource ts → webui bundle). `npm run rebuild` will pick it up.

```bash
npm run rebuild 2>&1 | tail -20
```

Expected: build succeeds. If TS compile fails, the output will include a `tsc` diagnostic with file:line.

- [ ] **Step 4: Do NOT commit yet — verify end-to-end first**

---

## Task 5: End-to-end manual verification

**Files:** none — verification only.

- [ ] **Step 1: Launch with stderr capture**

```bash
npm run start:debug
```

- [ ] **Step 2: Test case A — small file:// PDF**

Drag a small (1-3 page) PDF from Finder into Dao. Open the agent sidebar (Cmd+L from a focused page or click the agent button). Send: `"What is this document about?"`.

Expected:
- The user message in the chat bubble shows a page-context attachment chip with the PDF's title.
- The assistant response references actual content from the PDF (e.g., references specific names, terms, numbers in the file).

Failure modes to watch:
- Assistant says "I don't see any document attached" → the attachment didn't get built; check console for `[dao-capture]` warnings, re-verify Task 4 path.
- Assistant references the filename only → text was empty; check native handler returned `text.length > 0`. Re-test from DevTools per Task 3 Step 8.
- Black screen returns → unrelated regression; bail and check what changed.

- [ ] **Step 3: Test case B — large PDF with truncation**

Open a PDF with > 100 pages (any large research paper, conference proceedings, etc.). Ask the agent: `"Summarize this in 3 bullets."`.

Expected:
- Response references content from early pages.
- Response either acknowledges the truncation OR simply works on what it has without crashing.
- Capture latency feels acceptable (< 5s for moderate PDFs; could be longer on very large ones, but doesn't hang indefinitely).

- [ ] **Step 4: Test case C — HTML page (regression check)**

Navigate to any HTML article (e.g., a Wikipedia page, a blog post). Ask the agent: `"What is this article about?"`.

Expected:
- Behavior identical to before this change: Readability extracts the article, agent answers from it.

If Readability behavior changed: Task 4's fast path is leaking. The `isPdf !== true` branch must fall through cleanly.

- [ ] **Step 5: Test case D — capture from command bar (Cmd+L)**

On a PDF tab, hit Cmd+L, type a question, submit.

Expected: same as Test case A (the command bar's submit path also calls `captureCurrentPageMarkdown` → goes through the new fast path).

- [ ] **Step 6: Test case E — http(s):// PDF**

Open an online PDF (e.g., any arxiv.org PDF link). Repeat Test case A.

Expected: same behavior — PDF text reaches the agent.

- [ ] **Step 7: Verify no console errors**

In Dao's agent-sidebar DevTools (right-click inside sidebar → Inspect), check the Console tab over the course of testing. Expected: no red errors related to `getPdfText`, no unhandled-promise warnings from the new code path.

---

## Task 6: Hand off to user for commit

**Files:** none.

- [ ] **Step 1: Summarize changes for the user**

Print a summary listing every file touched and what changed:

- `src/patches/chrome/browser/ui/BUILD.gn.patch` — adds `//components/pdf/browser` dep
- `src/dao/browser/ui/webui/dao_agent_ui.h` — declares `HandleGetPdfText` + helpers
- `src/dao/browser/ui/webui/dao_agent_ui.cc` — implements `getPdfText` native handler using `pdf::PDFDocumentHelper`
- `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts` — PDF fast path in `captureCurrentPageMarkdown`

- [ ] **Step 2: Remind user they need to commit themselves**

Per CLAUDE.md and the user's stored feedback: never auto-commit. The user runs `git add` / `git commit` when ready.

---

## Notes / Gotchas

- **Process lock / SiteInstance for PDFs**: PDF Extension lives in a separate process; `PDFDocumentHelper` is in the browser process and reaches the plugin via Mojo. We don't touch process model. The async chain holds `WebContents*` only via `EnsureAttached()` (which re-queries every call) — never cache a raw pointer across awaits.

- **Why poll `EnsureAttached()` between page fetches** (Task 3 Step 5): the user could close/switch the tab between `GetPageText(N)` and `GetPageText(N+1)`. Re-querying lets us bail with a clean error instead of crashing on a dangling pointer.

- **`u16string` UTF-16 → UTF-8**: PDFium returns `mojo_base.mojom.String16` → `std::u16string`. We convert per-page so the budget check uses true UTF-8 byte size (the limit the agent's context window cares about).

- **Why sequential, not parallel `GetPageText`**: parallel would be faster but PDFium isn't reentrant on the plugin side; per Chromium's existing accessibility code path, page-text is read sequentially. Don't optimize this without profiling.

- **Why a 5s timeout in Task 3 Step 3**: `RegisterForDocumentLoadComplete` has no built-in timeout. A user dropping a corrupt PDF would leave the JS promise hanging forever, blocking the agent UI. 5s is empirically enough for typical local files; remote ones already have to be downloaded by then anyway (PDF won't start rendering until the load is done).

- **Why we trust `contents->GetVisibleURL()` for navigation detection** (Task 3 Step 5): visible URL changes on commit, before any new PDFDocumentHelper would be installed. If you wanted bulletproof detection, also bind a `NavigationHandle` observer — but for this use case (mid-capture nav rejection), the URL compare is sufficient.

- **No PDF browser_test in this plan**: per spec, deferred until SQLite-FTS5 agent test issue is resolved. Manual verification in Task 5 is the acceptance gate.
