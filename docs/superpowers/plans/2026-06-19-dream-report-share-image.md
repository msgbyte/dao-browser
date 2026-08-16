# Dream Report Share Image Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Copy image" affordance to Dream Report pages that copies a branded PNG of the report body to the clipboard.

**Architecture:** Keep `dao_share_image.ts` as the shared image-rendering module. Add a dream-report template and a shared PNG clipboard helper, then wire `dao_dream_app.ts` to call them for the current loaded report. No C++, GN, database, or native bridge changes are needed.

**Tech Stack:** Dao Agent WebUI, Lit, TypeScript, Vitest, OffscreenCanvas, ClipboardItem, existing Agent i18n dictionaries.

## Global Constraints

- Communicate with the user in Chinese; write source code, comments, commit messages, PR titles, and documentation in English unless explicitly asked otherwise.
- Do not edit `engine/`.
- Do not run `autoninja`, `ninja`, `siso`, direct Chromium build tools, or `gn gen`.
- For this WebUI-only change, do not run `npm run rebuild` unless implementation unexpectedly touches C++, GN, or Chromium integration patches.
- Never run bare `npm run export`; this plan does not require export.
- Do not run `i18n.sh`.
- Do not hand-edit generated vendor files under `src/dao/browser/ui/webui/resources/agent/vendor/`.
- Do not create branches or worktrees.
- Do not run state-changing git commands such as `git add`, `git commit`, `git push`, `git stash`, `git checkout`, `git reset`, or `git apply` unless the latest user message explicitly authorizes the exact action.
- Because state-changing git commands are not authorized, this plan uses read-only `git diff` / `git status` review steps instead of commit steps.
- The share image contains only title, date, report Markdown body, and the Dao Browser footer. It must not include the history list, habit controls, habit feedback state, debug material, page buttons, scroll position, or any other interactive UI state.
- The button must not fall back to copying text when image copy fails.

---

## File Structure

- Modify `src/dao/browser/ui/webui/resources/agent/dao_share_image.ts`: add `DreamReportShareContext`, add `renderDreamReportShareImage()`, add `copyPngBlobToClipboard()`, and factor footer/body painting enough for chat and dream templates to share the implementation path.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts`: add rendering tests for the dream report template and clipboard-helper tests.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`: import the share helpers, add copy state, render the button in the header only when a report is loaded, and call the shared render/copy flow.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`: mock the share helpers and test visibility, success, failure, and privacy of the passed context.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`: add four English strings.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`: add four Simplified Chinese strings.

## Task 1: Shared Dream Report Image Renderer

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_share_image.ts`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts`

**Interfaces:**
- Consumes: existing `renderShareImage(ctx: ShareContext): Promise<Blob>` and existing Markdown block rendering helpers in `dao_share_image.ts`.
- Produces:
  - `export interface DreamReportShareContext { title: string; dateLabel: string; markdown: string; footer: string; }`
  - `export async function renderDreamReportShareImage(ctx: DreamReportShareContext): Promise<Blob>`
  - `export async function copyPngBlobToClipboard(blob: Blob): Promise<void>`

- [ ] **Step 1: Add failing dream-template and clipboard tests**

In `src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts`, update the import:

```ts
import {
  copyPngBlobToClipboard,
  renderDreamReportShareImage,
  renderShareImage,
} from '../dao_share_image.js';
```

Add these helpers near `renderOps()`:

```ts
async function renderDreamOps(markdown: string, dark = false):
    Promise<PaintOp[]> {
  Object.defineProperty(window, 'matchMedia', {
    configurable: true,
    value: (query: string) => ({
      matches: dark && query.includes('prefers-color-scheme: dark'),
      media: query,
      onchange: null,
      addEventListener: () => {},
      removeEventListener: () => {},
      addListener: () => {},
      removeListener: () => {},
      dispatchEvent: () => false,
    }),
  });

  const blob = await renderDreamReportShareImage({
    title: 'Dream Report',
    dateLabel: 'About your day on 2026-06-19',
    markdown,
    footer: 'Dreamed by Dao Browser',
  });
  return JSON.parse(await blob.text()).ops;
}

class FakeClipboardItem {
  constructor(readonly items: Record<string, Blob>) {}
}
```

Add these tests inside the existing `describe('renderShareImage visual parity', ...)` block:

```ts
it('renders dream report share images with title date body and dream footer',
   async () => {
     const ops = await renderDreamOps('## Focus\n\n- Read specs\n- Ship UI');
     const texts =
         ops.filter(op => op.type === 'fillText').map(op => op.text);

     expect(texts).toContain('Dream Report');
     expect(texts).toContain('About your day on 2026-06-19');
     expect(texts).toContain('Focus');
     expect(texts).toContain('Read specs');
     expect(texts).toContain('Ship UI');
     expect(texts).toContain('Dreamed by Dao Browser');
     expect(texts).not.toContain('Answered by Dao Browser');
   });

it('does not draw a chat user bubble for dream report share images',
   async () => {
     const ops = await renderDreamOps('Report body');
     const bubbleRects = ops.filter(op => {
       return op.type === 'fill' && op.fillStyle === 'rgb(70, 120, 190)';
     });
     expect(bubbleRects).toHaveLength(0);
   });

it('copies png blobs through ClipboardItem', async () => {
  const writes: unknown[][] = [];
  Object.defineProperty(window, 'ClipboardItem', {
    configurable: true,
    value: FakeClipboardItem,
  });
  Object.defineProperty(navigator, 'clipboard', {
    configurable: true,
    value: {write: vi.fn(async (items: unknown[]) => writes.push(items))},
  });

  const blob = new Blob(['png'], {type: 'image/png'});
  await copyPngBlobToClipboard(blob);

  expect(writes).toHaveLength(1);
  const item = writes[0]![0] as FakeClipboardItem;
  expect(item.items['image/png']).toBe(blob);
});

it('rejects png copy when the image clipboard API is unavailable', async () => {
  Object.defineProperty(window, 'ClipboardItem', {
    configurable: true,
    value: undefined,
  });
  Object.defineProperty(navigator, 'clipboard', {
    configurable: true,
    value: {write: vi.fn()},
  });

  await expect(copyPngBlobToClipboard(new Blob(['png'])))
      .rejects.toThrow('ClipboardItem API unavailable');
});
```

Also update the Vitest import at the top:

```ts
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
```

- [ ] **Step 2: Run the focused test and verify it fails for missing exports**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts
```

Expected: FAIL because `renderDreamReportShareImage` and `copyPngBlobToClipboard` are not exported yet.

- [ ] **Step 3: Add the shared public interfaces and helpers**

In `src/dao/browser/ui/webui/resources/agent/dao_share_image.ts`, add this interface after `ShareContext`:

```ts
export interface DreamReportShareContext {
  title: string;
  dateLabel: string;
  markdown: string;
  footer: string;
}
```

Add these constants near the existing canvas layout constants:

```ts
const REPORT_TITLE_FONT_SIZE = 34;
const REPORT_DATE_FONT_SIZE = 14;
const REPORT_TITLE_LINE_HEIGHT = 42;
const REPORT_DATE_LINE_HEIGHT = 20;
const REPORT_HEADER_TO_BODY = 44;
```

Add these helper types and functions before the public entry section:

```ts
interface MarkdownLayout {
  blocks: Block[];
  height: number;
}

function layoutMarkdown(
    ctx: CanvasRenderingContext2D, markdown: string, maxWidth: number,
    textColor: string, baseSize: number, baseLineHeight: number,
    theme: ShareTheme): MarkdownLayout {
  const tokens = lexMarkdown(markdown || '—');
  const blocks = buildBlocks(tokens, {
    ctx,
    maxWidth,
    textColor,
    baseSize,
    baseLineHeight,
    theme,
  });
  return {blocks, height: sumBlockHeight(blocks)};
}

function paintMarkdownBlocks(
    g: CanvasRenderingContext2D, blocks: Block[], x: number, y: number,
    maxWidth: number) {
  let cy = y;
  for (const b of blocks) {
    b.paint(g, x, cy, maxWidth);
    cy += b.height;
  }
}

function paintFooter(
    g: CanvasRenderingContext2D, theme: ShareTheme, totalHeight: number,
    footerText: string) {
  g.fillStyle = theme.footerBg;
  g.fillRect(
      0, totalHeight - FOOTER_HEIGHT, SHARE_CANVAS_WIDTH, FOOTER_HEIGHT);
  g.font = `12px ${FONT_FAMILY}`;
  g.fillStyle = theme.footerFg;
  g.textBaseline = 'middle';
  g.fillText(
      footerText, OUTER_PADDING, totalHeight - FOOTER_HEIGHT / 2);
}
```

Add this exported clipboard helper near the public exports:

```ts
export async function copyPngBlobToClipboard(blob: Blob): Promise<void> {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const ClipboardItemCtor = (window as any).ClipboardItem;
  if (!ClipboardItemCtor || !navigator.clipboard?.write) {
    throw new Error('ClipboardItem API unavailable');
  }
  await navigator.clipboard.write(
      [new ClipboardItemCtor({'image/png': blob})]);
}
```

- [ ] **Step 4: Refactor the existing chat renderer to use shared helpers**

Inside `renderShareImage(ctx: ShareContext)`, replace the answer block setup:

```ts
  const answerTokens = lexMarkdown(ctx.answer || '—');
  const answerBlocks = buildBlocks(answerTokens, {
    ctx: measure,
    maxWidth: answerMaxWidth,
    textColor: theme.text,
    baseSize: answerSize,
    baseLineHeight: answerLineHeight,
    theme,
  });
  const answerHeight = sumBlockHeight(answerBlocks);
```

with:

```ts
  const answerLayout = layoutMarkdown(
      measure, ctx.answer || '—', answerMaxWidth, theme.text, answerSize,
      answerLineHeight, theme);
  const answerBlocks = answerLayout.blocks;
  const answerHeight = answerLayout.height;
```

Replace the clipped answer paint loop:

```ts
  let cy = answerY;
  for (const b of answerBlocks) {
    b.paint(g, OUTER_PADDING, cy, answerMaxWidth);
    cy += b.height;
  }
```

with:

```ts
  paintMarkdownBlocks(g, answerBlocks, OUTER_PADDING, answerY, answerMaxWidth);
```

Replace the footer drawing at the end of `renderShareImage()`:

```ts
  g.fillStyle = theme.footerBg;
  g.fillRect(
      0, totalHeight - FOOTER_HEIGHT, SHARE_CANVAS_WIDTH, FOOTER_HEIGHT);
  g.font = `12px ${FONT_FAMILY}`;
  g.fillStyle = theme.footerFg;
  g.textBaseline = 'middle';
  g.fillText(
      'Answered by Dao Browser', OUTER_PADDING,
      totalHeight - FOOTER_HEIGHT / 2);
```

with:

```ts
  paintFooter(g, theme, totalHeight, 'Answered by Dao Browser');
```

- [ ] **Step 5: Add the dream report renderer**

Add this export after `renderShareImage()`:

```ts
export async function renderDreamReportShareImage(
    ctx: DreamReportShareContext): Promise<Blob> {
  const theme = currentShareTheme();
  const measureCanvas = createCanvas(SHARE_CANVAS_WIDTH, 16);
  const measure =
      measureCanvas.getContext('2d') as unknown as CanvasRenderingContext2D;

  const bodySize = 18;
  const bodyLineHeight = Math.round(bodySize * 1.6);
  const bodyMaxWidth = SHARE_CANVAS_WIDTH - OUTER_PADDING * 2;
  const bodyLayout = layoutMarkdown(
      measure, ctx.markdown || '—', bodyMaxWidth, theme.text, bodySize,
      bodyLineHeight, theme);

  const headerHeight = REPORT_TITLE_LINE_HEIGHT + REPORT_DATE_LINE_HEIGHT +
      REPORT_HEADER_TO_BODY;
  const contentHeight =
      OUTER_PADDING + headerHeight + bodyLayout.height + OUTER_PADDING;
  const totalHeight =
      Math.min(contentHeight + FOOTER_HEIGHT, SHARE_CANVAS_MAX_HEIGHT);
  const truncated = contentHeight + FOOTER_HEIGHT > SHARE_CANVAS_MAX_HEIGHT;

  const dpr = globalThis.devicePixelRatio || 1;
  const canvas = createCanvas(SHARE_CANVAS_WIDTH * dpr, totalHeight * dpr);
  const g = canvas.getContext('2d') as unknown as CanvasRenderingContext2D;
  g.scale(dpr, dpr);

  g.fillStyle = theme.bg;
  g.fillRect(0, 0, SHARE_CANVAS_WIDTH, totalHeight);

  g.textAlign = 'left';
  g.textBaseline = 'alphabetic';
  g.fillStyle = theme.text;
  g.font = `700 ${REPORT_TITLE_FONT_SIZE}px ${FONT_FAMILY}`;
  g.fillText(ctx.title || 'Dream Report', OUTER_PADDING,
             OUTER_PADDING + REPORT_TITLE_FONT_SIZE);

  g.fillStyle = theme.textMuted;
  g.font = `${REPORT_DATE_FONT_SIZE}px ${FONT_FAMILY}`;
  g.fillText(
      ctx.dateLabel || '', OUTER_PADDING,
      OUTER_PADDING + REPORT_TITLE_LINE_HEIGHT + REPORT_DATE_FONT_SIZE);

  const bodyY = OUTER_PADDING + headerHeight;
  const bodyPaintMax =
      truncated ? totalHeight - FOOTER_HEIGHT - OVERFLOW_FADE_HEIGHT / 2 :
                  totalHeight - FOOTER_HEIGHT - OUTER_PADDING;
  g.save();
  g.beginPath();
  g.rect(OUTER_PADDING, bodyY, bodyMaxWidth, bodyPaintMax - bodyY);
  g.clip();
  paintMarkdownBlocks(g, bodyLayout.blocks, OUTER_PADDING, bodyY, bodyMaxWidth);
  g.restore();

  if (truncated) {
    const fadeTop = totalHeight - FOOTER_HEIGHT - OVERFLOW_FADE_HEIGHT;
    const grad = g.createLinearGradient(
        0, fadeTop, 0, totalHeight - FOOTER_HEIGHT);
    grad.addColorStop(0, theme.bgTransparent);
    grad.addColorStop(1, theme.bg);
    g.fillStyle = grad;
    g.fillRect(0, fadeTop, SHARE_CANVAS_WIDTH, OVERFLOW_FADE_HEIGHT);
    g.font = `24px ${FONT_FAMILY}`;
    g.fillStyle = theme.textVeryMuted;
    g.textAlign = 'center';
    g.fillText(
        '…', SHARE_CANVAS_WIDTH / 2,
        totalHeight - FOOTER_HEIGHT - 48);
    g.textAlign = 'left';
  }

  paintFooter(g, theme, totalHeight, ctx.footer || 'Dreamed by Dao Browser');

  return canvasToBlob(canvas);
}
```

- [ ] **Step 6: Run the focused share-image test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts
```

Expected: PASS for all tests in `dao_share_image.test.ts`.

- [ ] **Step 7: Review the task diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/dao_share_image.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts
```

Expected: diff only contains the shared renderer, dream renderer, clipboard helper, and related tests.

## Task 2: Dream Report Page Button and i18n

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`

**Interfaces:**
- Consumes:
  - `renderDreamReportShareImage(ctx: DreamReportShareContext): Promise<Blob>` from Task 1.
  - `copyPngBlobToClipboard(blob: Blob): Promise<void>` from Task 1.
  - existing `DreamReportData.reportMarkdown` and `DreamReportData.dreamDate`.
- Produces:
  - Dream page header button shown only when `report_` is non-null.
  - `shareStatus_: 'idle'|'copying'|'copied'|'failed'` internal Lit state.
  - New i18n keys: `dream.page.copy_image`, `dream.page.copy_image_copied`, `dream.page.copy_image_failed`, `dream.share.footer`.

- [ ] **Step 1: Add failing Dream app tests**

In `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`, extend the hoisted mocks:

```ts
const shareMocks = vi.hoisted(() => ({
  renderDreamReportShareImage: vi.fn(async () =>
      new Blob(['png'], {type: 'image/png'})),
  copyPngBlobToClipboard: vi.fn(async () => undefined),
}));
```

Add this module mock before importing `../dao_dream_app.js`:

```ts
vi.mock('../dao_share_image.js', () => ({
  renderDreamReportShareImage: (...args: unknown[]) =>
      shareMocks.renderDreamReportShareImage(...args),
  copyPngBlobToClipboard: (...args: unknown[]) =>
      shareMocks.copyPngBlobToClipboard(...args),
}));
```

Update the i18n mock template map:

```ts
const templates: Record<string, string> = {
  'chat.dream.card_date': 'About {date}',
  'dream.page.copy_image': 'Copy image',
  'dream.page.copy_image_copied': 'Copied image',
  'dream.page.copy_image_failed': 'Copy failed',
  'dream.page.title': 'Dream Report',
  'dream.share.footer': 'Dreamed by Dao Browser',
};
```

Add reset lines in `beforeEach()`:

```ts
shareMocks.renderDreamReportShareImage.mockClear();
shareMocks.renderDreamReportShareImage.mockResolvedValue(
    new Blob(['png'], {type: 'image/png'}));
shareMocks.copyPngBlobToClipboard.mockClear();
shareMocks.copyPngBlobToClipboard.mockResolvedValue(undefined);
```

Add these tests to the `describe('dao-dream-app routing', ...)` block:

```ts
it('copies the current dream report as an image', async () => {
  bridgeMocks.callNative.mockResolvedValueOnce([
    {
      ...report('2026-06-19', habitCandidates()),
      reportMarkdown: '# Private report body',
      debugMaterialJson: '{"private":true}',
    },
  ]);

  const el = await mountDreamApp('/');
  const copyButton = Array.from(el.shadowRoot!.querySelectorAll('button'))
                         .find(button =>
                             button.textContent?.includes('Copy image'));
  expect(copyButton).toBeTruthy();

  copyButton!.click();
  await Promise.resolve();
  await Promise.resolve();
  await el.updateComplete;

  expect(shareMocks.renderDreamReportShareImage).toHaveBeenCalledWith({
    title: 'Dream Report',
    dateLabel: 'About 2026-06-19',
    markdown: '# Private report body',
    footer: 'Dreamed by Dao Browser',
  });
  const blob = await shareMocks.renderDreamReportShareImage.mock.results[0]!
                   .value;
  expect(shareMocks.copyPngBlobToClipboard).toHaveBeenCalledWith(blob);
  expect(el.shadowRoot!.textContent).toContain('Copied image');
});

it('shows copy failure when image clipboard write fails', async () => {
  bridgeMocks.callNative.mockResolvedValueOnce([report('2026-06-19')]);
  shareMocks.copyPngBlobToClipboard.mockRejectedValueOnce(
      new Error('no clipboard'));

  const el = await mountDreamApp('/');
  const copyButton = Array.from(el.shadowRoot!.querySelectorAll('button'))
                         .find(button =>
                             button.textContent?.includes('Copy image'));
  expect(copyButton).toBeTruthy();

  copyButton!.click();
  await Promise.resolve();
  await Promise.resolve();
  await el.updateComplete;

  expect(el.shadowRoot!.textContent).toContain('Copy failed');
});

it('does not render copy image while the dream page is empty', async () => {
  bridgeMocks.callNative.mockResolvedValueOnce([]);

  const el = await mountDreamApp('/');

  expect(el.shadowRoot!.textContent).not.toContain('Copy image');
  expect(shareMocks.renderDreamReportShareImage).not.toHaveBeenCalled();
});

it('does not render copy image when dream report loading fails', async () => {
  bridgeMocks.callNative.mockRejectedValueOnce(new Error('boom'));

  const el = await mountDreamApp('/');

  expect(el.shadowRoot!.textContent).not.toContain('Copy image');
  expect(shareMocks.renderDreamReportShareImage).not.toHaveBeenCalled();
});
```

- [ ] **Step 2: Run the focused Dream app test and verify it fails for missing UI**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
```

Expected: FAIL because `dao_dream_app.ts` does not import the share helpers or render the copy button yet.

- [ ] **Step 3: Add the share imports and state**

In `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`, add this import:

```ts
import {
  copyPngBlobToClipboard,
  renderDreamReportShareImage,
} from './dao_share_image.js';
```

Add this type after `type HabitState`:

```ts
type ShareStatus = 'idle'|'copying'|'copied'|'failed';
```

Add `shareStatus_` to Lit properties:

```ts
      shareStatus_: {type: String, state: true},
```

Add the declaration:

```ts
  declare private shareStatus_: ShareStatus;
```

Set the initial state in the constructor:

```ts
    this.shareStatus_ = 'idle';
```

- [ ] **Step 4: Add header button styling**

In the same file, replace the `.date` style block:

```css
      .date {
        color: rgba(30, 20, 40, 0.56);
        font-size: 13px;
        white-space: nowrap;
      }
```

with:

```css
      .header-actions {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 10px;
        flex-wrap: wrap;
      }

      .date {
        color: rgba(30, 20, 40, 0.56);
        font-size: 13px;
        white-space: nowrap;
      }

      .copy-image-button {
        height: 30px;
        padding: 0 12px;
        border: 1px solid rgba(70, 120, 190, 0.24);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.68);
        color: rgba(30, 20, 40, 0.80);
        font: inherit;
        font-size: 12px;
        cursor: pointer;
      }

      .copy-image-button:hover:not(:disabled) {
        background: rgba(255, 255, 255, 0.92);
        border-color: rgba(70, 120, 190, 0.42);
      }

      .copy-image-button:disabled {
        cursor: default;
        opacity: 0.68;
      }
```

Inside the existing `@media (max-width: 640px)` block, add:

```css
        .header-actions {
          justify-content: flex-start;
        }
```

- [ ] **Step 5: Add the copy action methods**

Add these private methods before `renderReportArticle_()`:

```ts
  private shareButtonLabel_() {
    switch (this.shareStatus_) {
      case 'copying':
        return t('dream.page.copy_image');
      case 'copied':
        return t('dream.page.copy_image_copied');
      case 'failed':
        return t('dream.page.copy_image_failed');
      default:
        return t('dream.page.copy_image');
    }
  }

  private resetShareStatusLater_(status: ShareStatus) {
    window.setTimeout(() => {
      if (this.shareStatus_ === status) {
        this.shareStatus_ = 'idle';
      }
    }, 2000);
  }

  private async copyReportImage_() {
    const report = this.report_;
    if (!report || this.shareStatus_ === 'copying') {
      return;
    }

    this.shareStatus_ = 'copying';
    try {
      const blob = await renderDreamReportShareImage({
        title: t('dream.page.title'),
        dateLabel: t('chat.dream.card_date', {date: report.dreamDate}),
        markdown: report.reportMarkdown,
        footer: t('dream.share.footer'),
      });
      await copyPngBlobToClipboard(blob);
      this.shareStatus_ = 'copied';
      this.resetShareStatusLater_('copied');
    } catch (e) {
      console.warn('[dao] dream report image copy failed', e);
      this.shareStatus_ = 'failed';
      this.resetShareStatusLater_('failed');
    }
  }
```

- [ ] **Step 6: Render the button only when a report is loaded**

In `render()`, replace the report date block:

```ts
          ${report ? html`
            <div class="date">
              ${t('chat.dream.card_date', {date: report.dreamDate})}
            </div>` : nothing}
```

with:

```ts
          ${report ? html`
            <div class="header-actions">
              <div class="date">
                ${t('chat.dream.card_date', {date: report.dreamDate})}
              </div>
              <button class="copy-image-button"
                  ?disabled=${this.shareStatus_ === 'copying'}
                  @click=${() => void this.copyReportImage_()}>
                ${this.shareButtonLabel_()}
              </button>
            </div>` : nothing}
```

- [ ] **Step 7: Add English i18n keys**

In `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`, add these keys after `dream.page.history_title`:

```ts
  'dream.page.copy_image': 'Copy image',
  'dream.page.copy_image_copied': 'Copied image',
  'dream.page.copy_image_failed': 'Copy failed',
  'dream.share.footer': 'Dreamed by Dao Browser',
```

- [ ] **Step 8: Add Simplified Chinese i18n keys**

In `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`, add these keys after `dream.page.history_title`:

```ts
  'dream.page.copy_image': '复制图片',
  'dream.page.copy_image_copied': '已复制图片',
  'dream.page.copy_image_failed': '复制失败',
  'dream.share.footer': 'Dao Browser 梦境生成',
```

- [ ] **Step 9: Run the focused Dream app test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
```

Expected: PASS for all tests in `dao_dream_app.test.ts`.

- [ ] **Step 10: Review the task diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts
```

Expected: diff only contains the Dream page button, share flow, tests, and the four English / Simplified Chinese i18n keys.

## Task 3: Use Shared Clipboard Helper From Chat and Run Final Verification

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
- Verify: `package.json` scripts `test:webui` and `lint:lit`

**Interfaces:**
- Consumes:
  - `copyPngBlobToClipboard(blob: Blob): Promise<void>` from Task 1.
  - existing `renderShareImage(ctx: ShareContext): Promise<Blob>`.
- Produces:
  - Chat "Copy as image" and Dream "Copy image" both use the same clipboard-write helper.

- [ ] **Step 1: Update the chat share-image mock for the new export**

In `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`, replace:

```ts
vi.mock('../dao_share_image.js', () => ({
  renderShareImage: vi.fn(),
}));
```

with:

```ts
vi.mock('../dao_share_image.js', () => ({
  copyPngBlobToClipboard: vi.fn(),
  renderShareImage: vi.fn(),
}));
```

- [ ] **Step 2: Import the clipboard helper in chat view**

In `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`, replace:

```ts
import {renderShareImage} from './dao_share_image.js';
```

with:

```ts
import {
  copyPngBlobToClipboard,
  renderShareImage,
} from './dao_share_image.js';
```

- [ ] **Step 3: Route chat image copy through the shared helper**

In `shareAssistantAsImage_(btn: HTMLButtonElement)`, replace:

```ts
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const ClipboardItemCtor = (window as any).ClipboardItem;
      if (!ClipboardItemCtor || !navigator.clipboard?.write) {
        throw new Error('ClipboardItem API unavailable');
      }
      await navigator.clipboard.write(
          [new ClipboardItemCtor({'image/png': blob})]);
```

with:

```ts
      await copyPngBlobToClipboard(blob);
```

- [ ] **Step 4: Run focused tests for all touched WebUI surfaces**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS for all three files.

- [ ] **Step 5: Run the required WebUI verification**

Run:

```bash
npm run test:webui
```

Expected: full Vitest WebUI suite passes.

- [ ] **Step 6: Run Lit lint verification**

Run:

```bash
npm run lint:lit
```

Expected: PASS with no Lit reactive field violations.

- [ ] **Step 7: Review final diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/dao_share_image.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_share_image.test.ts src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts
```

Expected: final diff implements only the shared image renderer, shared clipboard helper, Dream page copy button, i18n strings, and tests.

- [ ] **Step 8: Check working tree status without staging**

Run:

```bash
git status --short
```

Expected: only the planned files are modified or untracked. `docs/superpowers/*` may not appear because `.gitignore` ignores that directory.

## Notes For Execution

- Do not run `npm run rebuild` for this plan unless a future implementation change touches C++, GN, patches, or Chromium integration.
- Do not run `i18n.sh`; only `en.ts` and `zh-CN.ts` are manually updated.
- Do not run state-changing git commands unless the user explicitly authorizes the exact command.
- The implementation should preserve the existing `renderShareImage()` export so existing chat callers remain stable.
- The Dream share path must pass only `report.reportMarkdown` and `report.dreamDate` into `renderDreamReportShareImage()`.
