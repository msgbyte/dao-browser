// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Renders a Q/A pair into a branded PNG for clipboard sharing. Pure
// module: no DOM dependencies beyond Canvas 2D.

export interface ShareSource {
  title: string;
  domain: string;
}

export interface ShareContext {
  question: string;
  source?: ShareSource;
  answer: string;
}

// Canvas layout constants. All values are in CSS pixels; the canvas is
// scaled by devicePixelRatio on paint.
export const SHARE_CANVAS_WIDTH = 1200;
export const SHARE_CANVAS_MAX_HEIGHT = 2400;
const OUTER_PADDING = 80;
const BUBBLE_MAX_WIDTH = 720;
const BUBBLE_PADDING_X = 28;
const BUBBLE_PADDING_Y = 20;
const BUBBLE_RADIUS = 14;
const GAP_BUBBLE_TO_SOURCE = 8;
const GAP_SOURCE_TO_ANSWER = 48;
const GAP_BUBBLE_TO_ANSWER = 48;
const PARAGRAPH_SPACING = 20;
const FOOTER_HEIGHT = 48;
const OVERFLOW_FADE_HEIGHT = 120;
const SOURCE_FONT_SIZE = 12;
const SOURCE_LINE_HEIGHT = 14;

const FONT_FAMILY = 'system-ui, -apple-system, "Helvetica Neue", sans-serif';
const MONO_FAMILY = 'ui-monospace, SFMono-Regular, Menlo, monospace';

const BG_COLOR = 'rgb(231, 238, 245)';
const BG_COLOR_TRANSPARENT = 'rgba(231, 238, 245, 0)';
const TEXT_COLOR = 'rgb(30, 20, 40)';
const TEXT_MUTED = 'rgba(30, 20, 40, 0.4)';
const ACCENT_COLOR = 'rgb(70, 120, 190)';
const FOOTER_BG = 'rgb(54, 59, 64)';
const FOOTER_FG = 'rgb(255, 255, 255)';
const CODE_BG = 'rgba(0, 0, 0, 0.06)';

type RunStyle = 'normal' | 'bold' | 'code' | 'link';
interface TextRun {
  text: string;
  style: RunStyle;
}

// Very small Markdown-ish inline parser. Supported:
//   **bold**            -> bold
//   `inline code`       -> code
//   [text](url)         -> link (text only; the URL is discarded because
//                          the output is a static image)
// Anything else is emitted verbatim as a normal run. Escapes ('\*', '\`')
// are NOT supported — the agent almost never emits them and the image is
// a lossy preview, not a renderer.
function parseInline(src: string): TextRun[] {
  const runs: TextRun[] = [];
  let i = 0;
  let buf = '';
  const flush = () => {
    if (buf.length > 0) {
      runs.push({text: buf, style: 'normal'});
      buf = '';
    }
  };
  while (i < src.length) {
    const ch = src[i];
    if (ch === '*' && src[i + 1] === '*') {
      const end = src.indexOf('**', i + 2);
      if (end > i + 1) {
        flush();
        runs.push({text: src.slice(i + 2, end), style: 'bold'});
        i = end + 2;
        continue;
      }
    }
    if (ch === '`') {
      const end = src.indexOf('`', i + 1);
      if (end > i) {
        flush();
        runs.push({text: src.slice(i + 1, end), style: 'code'});
        i = end + 1;
        continue;
      }
    }
    if (ch === '[') {
      const closeBracket = src.indexOf(']', i + 1);
      if (closeBracket > i && src[closeBracket + 1] === '(') {
        const closeParen = src.indexOf(')', closeBracket + 2);
        if (closeParen > closeBracket) {
          flush();
          runs.push({
            text: src.slice(i + 1, closeBracket),
            style: 'link',
          });
          i = closeParen + 1;
          continue;
        }
      }
    }
    buf += ch;
    i++;
  }
  flush();
  return runs;
}

function fontFor(style: RunStyle, sizePx: number): string {
  switch (style) {
    case 'bold':
      return `600 ${sizePx}px ${FONT_FAMILY}`;
    case 'code':
      return `${sizePx - 2}px ${MONO_FAMILY}`;
    case 'link':
      return `${sizePx}px ${FONT_FAMILY}`;
    default:
      return `${sizePx}px ${FONT_FAMILY}`;
  }
}

interface LayoutLine {
  runs: TextRun[];
  width: number;
}

function wrapRuns(
    ctx: CanvasRenderingContext2D, runs: TextRun[], maxWidth: number,
    sizePx: number): LayoutLine[] {
  const lines: LayoutLine[] = [{runs: [], width: 0}];
  const currentLine = (): LayoutLine => {
    // lines always has at least one element; the non-null assertion is safe.
    return lines[lines.length - 1]!;
  };
  for (const run of runs) {
    ctx.font = fontFor(run.style, sizePx);
    const segments = run.text.split(/(\n|\s+)/);
    for (const seg of segments) {
      if (seg === '') continue;
      if (seg === '\n') {
        lines.push({runs: [], width: 0});
        continue;
      }
      const segWidth = ctx.measureText(seg).width;
      const last = currentLine();
      if (last.width === 0 && /^\s+$/.test(seg)) continue;
      if (last.width + segWidth > maxWidth && last.width > 0) {
        const trimmed = seg.replace(/^\s+/, '');
        lines.push({
          runs: [{text: trimmed, style: run.style}],
          width: ctx.measureText(trimmed).width,
        });
      } else {
        last.runs.push({text: seg, style: run.style});
        last.width += segWidth;
      }
    }
  }
  return lines;
}

function measureTextBlock(
    ctx: CanvasRenderingContext2D, text: string, maxWidth: number,
    sizePx: number, lineHeight: number):
    {lines: LayoutLine[]; height: number} {
  const paragraphs = text.split(/\n\n+/);
  const out: LayoutLine[] = [];
  let height = 0;
  paragraphs.forEach((p, idx) => {
    const runs = parseInline(p);
    const wrapped = wrapRuns(ctx, runs, maxWidth, sizePx);
    for (const line of wrapped) {
      out.push(line);
      height += lineHeight;
    }
    if (idx < paragraphs.length - 1) {
      out.push({runs: [], width: 0});
      height += PARAGRAPH_SPACING;
    }
  });
  return {lines: out, height};
}

function paintRoundedRect(
    ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number,
    r: number): void {
  const rr = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + rr, y);
  ctx.lineTo(x + w - rr, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + rr);
  ctx.lineTo(x + w, y + h - rr);
  ctx.quadraticCurveTo(x + w, y + h, x + w - rr, y + h);
  ctx.lineTo(x + rr, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - rr);
  ctx.lineTo(x, y + rr);
  ctx.quadraticCurveTo(x, y, x + rr, y);
  ctx.closePath();
  ctx.fill();
}

function paintLines(
    ctx: CanvasRenderingContext2D, lines: LayoutLine[], x: number, y: number,
    sizePx: number, lineHeight: number, color: string, align: 'left' | 'right',
    maxWidth: number): number {
  let cursorY = y;
  for (const line of lines) {
    if (line.runs.length === 0) {
      cursorY += PARAGRAPH_SPACING;
      continue;
    }
    let cursorX = align === 'right' ? x + maxWidth - line.width : x;
    for (const run of line.runs) {
      ctx.font = fontFor(run.style, sizePx);
      if (run.style === 'code') {
        const w = ctx.measureText(run.text).width;
        ctx.fillStyle = CODE_BG;
        paintRoundedRect(
            ctx, cursorX - 2, cursorY - sizePx + 2, w + 4, sizePx + 2, 4);
      }
      ctx.fillStyle = color;
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(run.text, cursorX, cursorY);
      cursorX += ctx.measureText(run.text).width;
    }
    cursorY += lineHeight;
  }
  return cursorY;
}

export async function renderShareImage(ctx: ShareContext): Promise<Blob> {
  const measureCanvas = createCanvas(SHARE_CANVAS_WIDTH, 16);
  const measure =
      measureCanvas.getContext('2d') as unknown as CanvasRenderingContext2D;

  const answerSize = 18;
  const answerLineHeight = Math.round(answerSize * 1.6);
  const answerMaxWidth = SHARE_CANVAS_WIDTH - OUTER_PADDING * 2;
  const answerBlock = measureTextBlock(
      measure, ctx.answer || '—', answerMaxWidth, answerSize,
      answerLineHeight);

  const bubbleSize = 18;
  const bubbleLineHeight = Math.round(bubbleSize * 1.5);
  const bubbleInnerMax = BUBBLE_MAX_WIDTH - BUBBLE_PADDING_X * 2;
  const bubbleBlock = measureTextBlock(
      measure, ctx.question || '—', bubbleInnerMax, bubbleSize,
      bubbleLineHeight);
  const bubbleInnerWidth = Math.min(
      bubbleInnerMax,
      bubbleBlock.lines.reduce((m, l) => Math.max(m, l.width), 0));
  const bubbleOuterWidth = bubbleInnerWidth + BUBBLE_PADDING_X * 2;
  const bubbleOuterHeight = bubbleBlock.height + BUBBLE_PADDING_Y * 2;

  let y = OUTER_PADDING;
  const bubbleX =
      SHARE_CANVAS_WIDTH - OUTER_PADDING - bubbleOuterWidth;
  const bubbleY = y;
  y += bubbleOuterHeight;

  if (ctx.source) {
    y += GAP_BUBBLE_TO_SOURCE;
    y += SOURCE_LINE_HEIGHT;
    y += GAP_SOURCE_TO_ANSWER;
  } else {
    y += GAP_BUBBLE_TO_ANSWER;
  }

  const answerY = y;
  y += answerBlock.height;
  y += OUTER_PADDING;

  const contentHeight = y;
  const totalHeight =
      Math.min(contentHeight + FOOTER_HEIGHT, SHARE_CANVAS_MAX_HEIGHT);
  const truncated = contentHeight + FOOTER_HEIGHT > SHARE_CANVAS_MAX_HEIGHT;

  const dpr = globalThis.devicePixelRatio || 1;
  const canvas =
      createCanvas(SHARE_CANVAS_WIDTH * dpr, totalHeight * dpr);
  const g =
      canvas.getContext('2d') as unknown as CanvasRenderingContext2D;
  g.scale(dpr, dpr);

  g.fillStyle = BG_COLOR;
  g.fillRect(0, 0, SHARE_CANVAS_WIDTH, totalHeight);

  g.fillStyle = ACCENT_COLOR;
  paintRoundedRect(
      g, bubbleX, bubbleY, bubbleOuterWidth, bubbleOuterHeight, BUBBLE_RADIUS);
  paintLines(
      g, bubbleBlock.lines,
      bubbleX + BUBBLE_PADDING_X,
      bubbleY + BUBBLE_PADDING_Y + bubbleSize,
      bubbleSize, bubbleLineHeight, '#ffffff', 'left', bubbleInnerWidth);

  if (ctx.source) {
    g.font = `${SOURCE_FONT_SIZE}px ${FONT_FAMILY}`;
    g.fillStyle = TEXT_MUTED;
    g.textBaseline = 'alphabetic';
    const sourceText = `From ${ctx.source.domain}`;
    const w = g.measureText(sourceText).width;
    const sourceY =
        bubbleY + bubbleOuterHeight + GAP_BUBBLE_TO_SOURCE + SOURCE_FONT_SIZE;
    g.fillText(
        sourceText, SHARE_CANVAS_WIDTH - OUTER_PADDING - w, sourceY);
  }

  const answerPaintMax =
      truncated ? totalHeight - FOOTER_HEIGHT - OVERFLOW_FADE_HEIGHT / 2
                : totalHeight - FOOTER_HEIGHT - OUTER_PADDING;
  g.save();
  g.beginPath();
  g.rect(
      OUTER_PADDING, answerY, answerMaxWidth, answerPaintMax - answerY);
  g.clip();
  paintLines(
      g, answerBlock.lines, OUTER_PADDING, answerY + answerSize,
      answerSize, answerLineHeight, TEXT_COLOR, 'left', answerMaxWidth);
  g.restore();

  if (truncated) {
    const fadeTop = totalHeight - FOOTER_HEIGHT - OVERFLOW_FADE_HEIGHT;
    const grad = g.createLinearGradient(
        0, fadeTop, 0, totalHeight - FOOTER_HEIGHT);
    grad.addColorStop(0, BG_COLOR_TRANSPARENT);
    grad.addColorStop(1, BG_COLOR);
    g.fillStyle = grad;
    g.fillRect(0, fadeTop, SHARE_CANVAS_WIDTH, OVERFLOW_FADE_HEIGHT);
    g.font = `24px ${FONT_FAMILY}`;
    g.fillStyle = TEXT_MUTED;
    g.textAlign = 'center';
    g.fillText(
        '…', SHARE_CANVAS_WIDTH / 2,
        totalHeight - FOOTER_HEIGHT - 48);
    g.textAlign = 'left';
  }

  g.fillStyle = FOOTER_BG;
  g.fillRect(
      0, totalHeight - FOOTER_HEIGHT, SHARE_CANVAS_WIDTH, FOOTER_HEIGHT);
  g.font = `12px ${FONT_FAMILY}`;
  g.fillStyle = FOOTER_FG;
  g.textBaseline = 'middle';
  g.fillText(
      'Answered by Dao Browser', OUTER_PADDING,
      totalHeight - FOOTER_HEIGHT / 2);

  return canvasToBlob(canvas);
}

function createCanvas(w: number, h: number): OffscreenCanvas {
  return new OffscreenCanvas(w, h);
}

function canvasToBlob(canvas: OffscreenCanvas): Promise<Blob> {
  return canvas.convertToBlob({type: 'image/png'});
}
