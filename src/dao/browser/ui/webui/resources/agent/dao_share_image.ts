// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Renders a Q/A pair into a branded PNG for clipboard sharing. Drives the
// "copy as image" affordance under each assistant bubble: the user
// question, the source page (if captured) and the answer are laid out as
// a vertically-stacked, branded canvas and converted to a PNG blob ready
// for navigator.clipboard.write.
//
// Block-level Markdown is parsed via the same `marked` singleton mini-lit
// uses for the chat bubble, so the image stays visually faithful to what
// the user sees: headings, lists (incl. nested + ordered), blockquotes,
// horizontal rules, fenced code blocks (with light syntax tinting) and
// inline bold/italic/code/link. The user bubble runs the same renderer
// so multi-line pastes / formatted prompts no longer collapse to a wall
// of text.

import {marked} from './vendor/pi_runtime_bundle.js';

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
export const SHARE_CANVAS_MAX_HEIGHT = 4000;
const OUTER_PADDING = 80;
const BUBBLE_MAX_WIDTH = 720;
const BUBBLE_PADDING_X = 28;
const BUBBLE_PADDING_Y = 20;
const BUBBLE_RADIUS = 14;
const GAP_BUBBLE_TO_SOURCE = 8;
const GAP_SOURCE_TO_ANSWER = 48;
const GAP_BUBBLE_TO_ANSWER = 48;
const PARAGRAPH_SPACING = 14;
const FOOTER_HEIGHT = 48;
const OVERFLOW_FADE_HEIGHT = 120;
const SOURCE_FONT_SIZE = 12;
const SOURCE_LINE_HEIGHT = 14;

const FONT_FAMILY = 'system-ui, -apple-system, "Helvetica Neue", sans-serif';
const MONO_FAMILY = 'ui-monospace, SFMono-Regular, Menlo, monospace';

const BG_COLOR = 'rgb(231, 238, 245)';
const BG_COLOR_TRANSPARENT = 'rgba(231, 238, 245, 0)';
const TEXT_COLOR = 'rgb(30, 20, 40)';
const TEXT_MUTED = 'rgba(30, 20, 40, 0.55)';
const TEXT_VERY_MUTED = 'rgba(30, 20, 40, 0.4)';
const ACCENT_COLOR = 'rgb(70, 120, 190)';
const FOOTER_BG = 'rgb(54, 59, 64)';
const FOOTER_FG = 'rgb(255, 255, 255)';
const INLINE_CODE_COLOR = 'rgba(30, 20, 40, 0.72)';
const CODE_BLOCK_BG = 'rgba(0, 0, 0, 0.05)';
const CODE_BLOCK_BORDER = 'rgba(0, 0, 0, 0.08)';
const HR_COLOR = 'rgba(0, 0, 0, 0.12)';
const BLOCKQUOTE_BAR = 'rgba(30, 20, 40, 0.25)';

// Lightweight syntax palette. We only colorize a few categories the eye
// uses to scan code; the rest stays in TEXT_COLOR. Picked to look correct
// in both light and dark previews (the canvas is light-mode for now).
const SYN_KEYWORD = 'rgb(170, 50, 130)';
const SYN_STRING = 'rgb(70, 130, 70)';
const SYN_COMMENT = 'rgba(30, 20, 40, 0.45)';
const SYN_NUMBER = 'rgb(180, 100, 30)';
const SYN_FUNCTION = 'rgb(70, 100, 180)';

type RunStyle = 'normal' | 'bold' | 'italic' | 'bold-italic' | 'code'
    | 'inline-code' | 'link' | 'strike';
interface TextRun {
  text: string;
  style: RunStyle;
  color?: string;  // overrides the block-level color (used by syntax tints)
}

interface LayoutLine {
  runs: TextRun[];
  width: number;
}

// One drawable block produced by the lexer pass. `paint` runs in the
// second pass once the canvas is sized; `height` is the contribution to
// the running content height during measurement. `trailingSpace` is the
// portion of `height` that's blank padding meant to separate this block
// from the next one — the LAST block in a container should drop it so
// the container doesn't end up with a phantom bottom margin (e.g. the
// user-question bubble used to render ~14px taller than the text needed).
interface Block {
  height: number;
  trailingSpace: number;
  paint(g: CanvasRenderingContext2D, x: number, y: number, w: number): void;
}

// Sum block heights, dropping the very last block's trailingSpace so the
// container doesn't inherit a stray bottom margin.
function sumBlockHeight(blocks: Block[]): number {
  let h = 0;
  for (const b of blocks) h += b.height;
  if (blocks.length > 0) h -= blocks[blocks.length - 1]!.trailingSpace;
  return h;
}

// --------------- inline token walker ---------------

// Walks marked's inline tokens (Strong/Em/Codespan/Link/Text/Br) and
// produces a flat TextRun stream that wrapRuns can lay out. We do not
// nest styles arbitrarily: bold+italic merges to `bold-italic`, anything
// inside a code span stays `code`, links keep their text but drop the
// URL (this is a static image).
function tokensToRuns(
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    tokens: any[]|undefined, base: RunStyle = 'normal'): TextRun[] {
  const out: TextRun[] = [];
  if (!tokens) return out;
  for (const tk of tokens) {
    if (!tk) continue;
    switch (tk.type) {
      case 'text': {
        if (tk.tokens && tk.tokens.length > 0) {
          out.push(...tokensToRuns(tk.tokens, base));
        } else {
          out.push({text: decodeEntities(tk.text || tk.raw || ''), style: base});
        }
        break;
      }
      case 'strong':
        out.push(...tokensToRuns(tk.tokens, mergeStyle(base, 'bold')));
        break;
      case 'em':
        out.push(...tokensToRuns(tk.tokens, mergeStyle(base, 'italic')));
        break;
      case 'del':
        out.push(...tokensToRuns(tk.tokens, 'strike'));
        break;
      case 'codespan':
        out.push({text: decodeEntities(tk.text || ''), style: 'inline-code'});
        break;
      case 'link':
        if (tk.tokens && tk.tokens.length > 0) {
          out.push(...tokensToRuns(tk.tokens, 'link'));
        } else {
          out.push({text: decodeEntities(tk.text || ''), style: 'link'});
        }
        break;
      case 'br':
        out.push({text: '\n', style: base});
        break;
      case 'escape':
        out.push({text: tk.text || '', style: base});
        break;
      case 'image':
        // Static image renderer can't draw remote images; show alt text.
        out.push({text: tk.text ? `[${tk.text}]` : '[image]', style: base});
        break;
      case 'html':
        // Strip tags, keep the inner text.
        out.push({text: stripHtml(tk.raw || ''), style: base});
        break;
      default:
        if (typeof tk.text === 'string') {
          out.push({text: tk.text, style: base});
        }
    }
  }
  return out;
}

function mergeStyle(a: RunStyle, b: RunStyle): RunStyle {
  if (a === 'code' || b === 'code') return 'code';
  if (a === 'inline-code' || b === 'inline-code') return 'inline-code';
  if (a === 'link' || b === 'link') return 'link';
  const isBold = a === 'bold' || a === 'bold-italic' || b === 'bold' ||
      b === 'bold-italic';
  const isItalic = a === 'italic' || a === 'bold-italic' || b === 'italic' ||
      b === 'bold-italic';
  if (isBold && isItalic) return 'bold-italic';
  if (isBold) return 'bold';
  if (isItalic) return 'italic';
  return 'normal';
}

function decodeEntities(s: string): string {
  return s.replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>')
      .replace(/&quot;/g, '"').replace(/&#39;/g, '\'').replace(/&nbsp;/g, ' ');
}

function stripHtml(s: string): string {
  return s.replace(/<[^>]*>/g, '');
}

// --------------- text measurement / wrapping ---------------

function fontFor(style: RunStyle, sizePx: number, weight?: number): string {
  switch (style) {
    case 'bold':
      return `${weight ?? 600} ${sizePx}px ${FONT_FAMILY}`;
    case 'italic':
      return `italic ${sizePx}px ${FONT_FAMILY}`;
    case 'bold-italic':
      return `italic ${weight ?? 600} ${sizePx}px ${FONT_FAMILY}`;
    case 'code':
    case 'inline-code':
      return `${sizePx - 2}px ${MONO_FAMILY}`;
    case 'strike':
    case 'link':
    case 'normal':
    default:
      return `${weight ?? 400} ${sizePx}px ${FONT_FAMILY}`;
  }
}

function wrapRuns(
    ctx: CanvasRenderingContext2D, runs: TextRun[], maxWidth: number,
    sizePx: number, weight?: number): LayoutLine[] {
  const lines: LayoutLine[] = [{runs: [], width: 0}];
  const cur = (): LayoutLine => lines[lines.length - 1]!;
  for (const run of runs) {
    ctx.font = fontFor(run.style, sizePx, weight);
    const segments = run.text.split(/(\n|\s+)/);
    for (const seg of segments) {
      if (seg === '') continue;
      if (seg === '\n') {
        lines.push({runs: [], width: 0});
        continue;
      }
      const segWidth = ctx.measureText(seg).width;
      const last = cur();
      if (last.width === 0 && /^\s+$/.test(seg)) continue;
      if (last.width + segWidth > maxWidth && last.width > 0) {
        const trimmed = seg.replace(/^\s+/, '');
        if (trimmed.length === 0) continue;
        // Long unbreakable run (e.g. a URL or CJK glyph stretch): hard-break
        // it character-by-character so the layout never overruns the gutter.
        if (ctx.measureText(trimmed).width > maxWidth) {
          let buf = '';
          for (const ch of trimmed) {
            if (ctx.measureText(buf + ch).width > maxWidth && buf.length > 0) {
              lines.push({
                runs: [{text: buf, style: run.style, color: run.color}],
                width: ctx.measureText(buf).width,
              });
              buf = ch;
            } else {
              buf += ch;
            }
          }
          if (buf.length > 0) {
            lines.push({
              runs: [{text: buf, style: run.style, color: run.color}],
              width: ctx.measureText(buf).width,
            });
          }
        } else {
          lines.push({
            runs: [{text: trimmed, style: run.style, color: run.color}],
            width: ctx.measureText(trimmed).width,
          });
        }
      } else {
        last.runs.push({text: seg, style: run.style, color: run.color});
        last.width += segWidth;
      }
    }
  }
  return lines;
}

function paintLine(
    g: CanvasRenderingContext2D, line: LayoutLine, x: number, y: number,
    sizePx: number, color: string, weight?: number): void {
  let cursorX = x;
  for (const run of line.runs) {
    g.font = fontFor(run.style, sizePx, weight);
    const w = g.measureText(run.text).width;
    // Inline code: the prior CODE_BG pill visually read as a text-selection
    // highlight against the light surface. Distinguish code via the
    // monospace font + a muted tint instead. White-on-blue bubbles keep
    // their bubble text color so code stays legible on accent. Code blocks
    // (style 'code', set by syntaxRuns) keep their syntax-tinted palette.
    g.fillStyle = run.color ||
        (run.style === 'link' ?
             ACCENT_COLOR :
             (run.style === 'inline-code' && color === TEXT_COLOR ?
                  INLINE_CODE_COLOR :
                  color));
    g.textBaseline = 'alphabetic';
    g.fillText(run.text, cursorX, y);
    if (run.style === 'strike') {
      g.strokeStyle = color;
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(cursorX, y - sizePx * 0.35);
      g.lineTo(cursorX + w, y - sizePx * 0.35);
      g.stroke();
    } else if (run.style === 'link') {
      // Subtle underline so links stay legible even without color contrast.
      g.strokeStyle = ACCENT_COLOR;
      g.lineWidth = 1;
      g.beginPath();
      g.moveTo(cursorX, y + 2);
      g.lineTo(cursorX + w, y + 2);
      g.stroke();
    }
    cursorX += w;
  }
}

// --------------- syntax tokenizer (regex-based) ---------------

// Single shared regex per category. We tokenize greedily left-to-right;
// the first match at the cursor wins. Heuristic-only: enough to give the
// eye color anchors when scanning a code block.
const KEYWORDS_JS = new Set([
  'function', 'return', 'const', 'let', 'var', 'if', 'else', 'for', 'while',
  'do', 'switch', 'case', 'break', 'continue', 'class', 'extends', 'super',
  'new', 'this', 'typeof', 'instanceof', 'in', 'of', 'try', 'catch',
  'finally', 'throw', 'async', 'await', 'import', 'export', 'from', 'as',
  'default', 'true', 'false', 'null', 'undefined', 'yield', 'static', 'void',
]);
const KEYWORDS_PY = new Set([
  'def', 'class', 'return', 'if', 'elif', 'else', 'for', 'while', 'try',
  'except', 'finally', 'raise', 'with', 'as', 'import', 'from', 'pass',
  'break', 'continue', 'lambda', 'yield', 'global', 'nonlocal', 'True',
  'False', 'None', 'and', 'or', 'not', 'in', 'is', 'async', 'await',
]);
const KEYWORDS_GO = new Set([
  'func', 'package', 'import', 'return', 'if', 'else', 'for', 'range',
  'switch', 'case', 'default', 'break', 'continue', 'struct', 'interface',
  'type', 'var', 'const', 'go', 'chan', 'select', 'defer', 'map', 'true',
  'false', 'nil',
]);
const KEYWORDS_RUST = new Set([
  'fn', 'let', 'mut', 'const', 'static', 'pub', 'struct', 'enum', 'trait',
  'impl', 'for', 'while', 'loop', 'if', 'else', 'match', 'return', 'use',
  'mod', 'crate', 'self', 'Self', 'true', 'false', 'as', 'in', 'ref',
  'where', 'async', 'await', 'move',
]);
const KEYWORDS_C = new Set([
  'int', 'float', 'double', 'char', 'long', 'short', 'unsigned', 'signed',
  'void', 'struct', 'union', 'enum', 'typedef', 'static', 'const', 'volatile',
  'return', 'if', 'else', 'for', 'while', 'do', 'switch', 'case', 'break',
  'continue', 'sizeof', 'goto', 'auto', 'register', 'extern', 'inline',
  'class', 'public', 'private', 'protected', 'namespace', 'using', 'template',
  'typename', 'new', 'delete', 'this', 'true', 'false', 'nullptr',
]);
const KEYWORDS_SHELL = new Set([
  'if', 'then', 'else', 'elif', 'fi', 'for', 'do', 'done', 'while', 'until',
  'case', 'esac', 'function', 'return', 'in', 'echo', 'exit', 'export',
  'local', 'readonly', 'unset',
]);

function keywordSetForLang(lang?: string): Set<string>|null {
  if (!lang) return null;
  const l = lang.toLowerCase().trim();
  if (l === 'js' || l === 'jsx' || l === 'ts' || l === 'tsx' ||
      l === 'javascript' || l === 'typescript' || l === 'node') {
    return KEYWORDS_JS;
  }
  if (l === 'py' || l === 'python') return KEYWORDS_PY;
  if (l === 'go' || l === 'golang') return KEYWORDS_GO;
  if (l === 'rs' || l === 'rust') return KEYWORDS_RUST;
  if (l === 'c' || l === 'cpp' || l === 'c++' || l === 'cxx' || l === 'h' ||
      l === 'hpp' || l === 'java' || l === 'cs' || l === 'csharp') {
    return KEYWORDS_C;
  }
  if (l === 'sh' || l === 'bash' || l === 'zsh' || l === 'shell') {
    return KEYWORDS_SHELL;
  }
  if (l === 'json') return null;  // Strings + numbers are enough for JSON.
  return null;
}

function syntaxRuns(text: string, lang?: string): TextRun[] {
  const keywords = keywordSetForLang(lang);
  const runs: TextRun[] = [];
  let buf = '';
  const flushBuf = (color?: string) => {
    if (buf.length > 0) {
      runs.push({text: buf, style: 'code', color});
      buf = '';
    }
  };
  let i = 0;
  const n = text.length;
  // Comment line prefix detection by language.
  const isShellLike = lang ? /^(sh|bash|zsh|shell|py|python|yaml|yml|toml|ruby|rb)$/i.test(lang) : false;
  const isHashComment = isShellLike;
  while (i < n) {
    const ch = text[i]!;
    // Line comments: // (most) or # (shell/python/yaml).
    if (ch === '/' && text[i + 1] === '/' && !isHashComment) {
      flushBuf();
      const end = text.indexOf('\n', i);
      const stop = end < 0 ? n : end;
      runs.push({text: text.slice(i, stop), style: 'code', color: SYN_COMMENT});
      i = stop;
      continue;
    }
    if (ch === '#' && isHashComment && (i === 0 || text[i - 1] === '\n')) {
      flushBuf();
      const end = text.indexOf('\n', i);
      const stop = end < 0 ? n : end;
      runs.push({text: text.slice(i, stop), style: 'code', color: SYN_COMMENT});
      i = stop;
      continue;
    }
    // Block comments: /* ... */
    if (ch === '/' && text[i + 1] === '*') {
      flushBuf();
      const end = text.indexOf('*/', i + 2);
      const stop = end < 0 ? n : end + 2;
      runs.push({text: text.slice(i, stop), style: 'code', color: SYN_COMMENT});
      i = stop;
      continue;
    }
    // Strings: ", ', `. No multi-line "; ` is allowed to span lines.
    if (ch === '"' || ch === '\'' || ch === '`') {
      flushBuf();
      const quote = ch;
      let j = i + 1;
      while (j < n) {
        if (text[j] === '\\' && j + 1 < n) { j += 2; continue; }
        if (text[j] === quote) { j++; break; }
        if (text[j] === '\n' && quote !== '`') { break; }
        j++;
      }
      runs.push({text: text.slice(i, j), style: 'code', color: SYN_STRING});
      i = j;
      continue;
    }
    // Numbers
    if ((ch >= '0' && ch <= '9') ||
        (ch === '.' && text[i + 1] && text[i + 1]! >= '0' &&
         text[i + 1]! <= '9')) {
      flushBuf();
      let j = i;
      while (j < n && /[0-9._xXa-fA-F]/.test(text[j]!)) j++;
      runs.push({text: text.slice(i, j), style: 'code', color: SYN_NUMBER});
      i = j;
      continue;
    }
    // Identifiers / keywords
    if (/[A-Za-z_$]/.test(ch)) {
      flushBuf();
      let j = i;
      while (j < n && /[A-Za-z0-9_$]/.test(text[j]!)) j++;
      const word = text.slice(i, j);
      if (keywords && keywords.has(word)) {
        runs.push({text: word, style: 'code', color: SYN_KEYWORD});
      } else if (text[j] === '(' && /[A-Za-z_$]/.test(word[0]!)) {
        runs.push({text: word, style: 'code', color: SYN_FUNCTION});
      } else {
        runs.push({text: word, style: 'code'});
      }
      i = j;
      continue;
    }
    buf += ch;
    i++;
  }
  flushBuf();
  return runs;
}

// --------------- block builders ---------------

interface BuildOpts {
  ctx: CanvasRenderingContext2D;
  maxWidth: number;
  textColor: string;
  baseSize: number;
  baseLineHeight: number;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildBlocks(tokens: any[], opts: BuildOpts): Block[] {
  const out: Block[] = [];
  for (const tk of tokens) {
    if (!tk) continue;
    if (tk.type === 'space') {
      out.push({
        height: PARAGRAPH_SPACING,
        trailingSpace: PARAGRAPH_SPACING,
        paint: () => {},
      });
      continue;
    }
    if (tk.type === 'hr') {
      out.push({
        height: PARAGRAPH_SPACING * 2,
        trailingSpace: PARAGRAPH_SPACING,
        paint(g, x, y, w) {
          g.strokeStyle = HR_COLOR;
          g.lineWidth = 1;
          g.beginPath();
          g.moveTo(x, y + PARAGRAPH_SPACING);
          g.lineTo(x + w, y + PARAGRAPH_SPACING);
          g.stroke();
        },
      });
      continue;
    }
    if (tk.type === 'heading') {
      out.push(buildHeading(tk, opts));
      continue;
    }
    if (tk.type === 'code') {
      out.push(buildCodeBlock(tk, opts));
      continue;
    }
    if (tk.type === 'blockquote') {
      out.push(buildBlockquote(tk, opts));
      continue;
    }
    if (tk.type === 'list') {
      out.push(buildList(tk, opts, 0));
      continue;
    }
    if (tk.type === 'paragraph' || tk.type === 'text') {
      out.push(buildParagraph(tk, opts));
      continue;
    }
    if (tk.type === 'html') {
      // Render visible text only, skip script/style blocks.
      const text = stripHtml(tk.raw || '').trim();
      if (text.length > 0) {
        out.push(buildParagraph({tokens: [{type: 'text', text}]}, opts));
      }
      continue;
    }
    // Tables would need their own grid layout — punt on them: render as
    // a single italic paragraph noting the omission so the user knows.
    if (tk.type === 'table') {
      out.push(buildParagraph(
          {tokens: [{type: 'em', tokens: [{type: 'text', text: '[table]'}]}]},
          opts));
      continue;
    }
  }
  return out;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildParagraph(
    tk: any, opts: BuildOpts, trailingSpacing = PARAGRAPH_SPACING): Block {
  const runs = tokensToRuns(tk.tokens);
  const lines =
      wrapRuns(opts.ctx, runs, opts.maxWidth, opts.baseSize);
  const height = lines.length * opts.baseLineHeight + trailingSpacing;
  return {
    height,
    trailingSpace: trailingSpacing,
    paint: (g, x, y, _w) => {
      let cy = y + opts.baseSize;
      for (const line of lines) {
        paintLine(g, line, x, cy, opts.baseSize, opts.textColor);
        cy += opts.baseLineHeight;
      }
    },
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildHeading(tk: any, opts: BuildOpts): Block {
  // h1 28, h2 24, h3 20, h4 18, h5 16, h6 14
  const sizeMap = [28, 28, 24, 20, 18, 16, 14];
  const size = sizeMap[Math.max(1, Math.min(6, tk.depth || 1))]!;
  const lineHeight = Math.round(size * 1.35);
  const runs = tokensToRuns(tk.tokens, 'bold');
  const lines = wrapRuns(opts.ctx, runs, opts.maxWidth, size, 700);
  const topPad = Math.round(size * 0.6);
  const botPad = Math.round(size * 0.3);
  const height = topPad + lines.length * lineHeight + botPad;
  return {
    height,
    trailingSpace: botPad,
    paint: (g, x, y, w) => {
      let cy = y + topPad + size;
      for (const line of lines) {
        paintLine(g, line, x, cy, size, opts.textColor, 700);
        cy += lineHeight;
      }
      // h1 / h2 get a subtle underline for visual weight.
      if (tk.depth <= 2) {
        g.strokeStyle = HR_COLOR;
        g.lineWidth = 1;
        g.beginPath();
        g.moveTo(x, y + height - 4);
        g.lineTo(x + w, y + height - 4);
        g.stroke();
      }
    },
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildCodeBlock(tk: any, opts: BuildOpts): Block {
  const PAD_X = 16;
  const PAD_Y = 12;
  const codeSize = Math.max(12, opts.baseSize - 2);
  const codeLineHeight = Math.round(codeSize * 1.5);
  const innerWidth = opts.maxWidth - PAD_X * 2;
  const raw = (tk.text || '').replace(/\t/g, '  ');
  const tinted = syntaxRuns(raw, tk.lang);
  // Code blocks use hard line breaks; preserve them by splitting on \n
  // before wrapping. Within a single source line we still soft-wrap long
  // strings so the layout never overruns.
  const lines: LayoutLine[] = [];
  let currentRuns: TextRun[] = [];
  const flushCurrent = () => {
    if (currentRuns.length === 0) {
      lines.push({runs: [], width: 0});
      return;
    }
    const wrapped =
        wrapRuns(opts.ctx, currentRuns, innerWidth, codeSize);
    for (const ln of wrapped) lines.push(ln);
    currentRuns = [];
  };
  for (const run of tinted) {
    const parts = run.text.split('\n');
    for (let pi = 0; pi < parts.length; pi++) {
      if (parts[pi]!.length > 0) {
        currentRuns.push({text: parts[pi]!, style: run.style, color: run.color});
      }
      if (pi < parts.length - 1) flushCurrent();
    }
  }
  flushCurrent();
  const langLabel = (tk.lang || '').toString();
  const labelHeight = langLabel ? 22 : 0;
  const blockHeight =
      labelHeight + PAD_Y * 2 + lines.length * codeLineHeight;
  const height = blockHeight + PARAGRAPH_SPACING;
  return {
    height,
    trailingSpace: PARAGRAPH_SPACING,
    paint: (g, x, y, _w) => {
      g.fillStyle = CODE_BLOCK_BG;
      paintRoundedRect(g, x, y, opts.maxWidth, blockHeight, 8);
      g.strokeStyle = CODE_BLOCK_BORDER;
      g.lineWidth = 1;
      strokeRoundedRect(g, x, y, opts.maxWidth, blockHeight, 8);
      if (langLabel) {
        g.font = `500 11px ${MONO_FAMILY}`;
        g.fillStyle = TEXT_VERY_MUTED;
        g.textBaseline = 'alphabetic';
        g.fillText(langLabel, x + PAD_X, y + 14);
      }
      let cy = y + labelHeight + PAD_Y + codeSize;
      for (const line of lines) {
        paintLine(g, line, x + PAD_X, cy, codeSize, opts.textColor);
        cy += codeLineHeight;
      }
    },
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildBlockquote(tk: any, opts: BuildOpts): Block {
  const INDENT = 16;
  const BAR_W = 3;
  const inner = buildBlocks(tk.tokens || [], {
    ...opts,
    maxWidth: opts.maxWidth - INDENT,
    textColor: TEXT_MUTED,
  });
  const innerHeight = inner.reduce((sum, b) => sum + b.height, 0);
  return {
    height: innerHeight + PARAGRAPH_SPACING / 2,
    trailingSpace: PARAGRAPH_SPACING / 2,
    paint: (g, x, y, _w) => {
      g.fillStyle = BLOCKQUOTE_BAR;
      g.fillRect(x, y + 2, BAR_W, innerHeight - 4);
      let cy = y;
      for (const b of inner) {
        b.paint(g, x + INDENT, cy, opts.maxWidth - INDENT);
        cy += b.height;
      }
    },
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function buildList(tk: any, opts: BuildOpts, depth: number): Block {
  const INDENT = 24;
  const MARKER_GAP = 6;
  const ordered = !!tk.ordered;
  const start = typeof tk.start === 'number' ? tk.start : 1;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const items: {marker: string; blocks: Block[]; height: number}[] = [];
  let totalHeight = 0;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  tk.items.forEach((item: any, idx: number) => {
    const marker = ordered ? `${start + idx}.` : (depth % 2 === 0 ? '•' : '◦');
    const itemOpts: BuildOpts = {
      ...opts,
      maxWidth: opts.maxWidth - INDENT,
    };
    // marked emits list items with a `text` token (inline) + optional nested
    // block tokens. Inline `text` tokens become tight paragraphs (no trailing
    // spacing) so list items stay compact; nested lists / code / quotes are
    // delegated to buildBlocksRecur which handles their own spacing.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const tokens = (item.tokens || []) as any[];
    const innerBlocks: Block[] = [];
    for (const t of tokens) {
      if (t.type === 'text') {
        const inlineTokens = Array.isArray(t.tokens) ?
            t.tokens :
            [{type: 'text', text: t.text}];
        innerBlocks.push(
            buildParagraph({tokens: inlineTokens}, itemOpts, 0));
      } else if (t.type === 'list') {
        innerBlocks.push(buildList(t, itemOpts, depth + 1));
      } else {
        innerBlocks.push(...buildBlocks([t], itemOpts));
      }
    }
    const h = innerBlocks.reduce((s, b) => s + b.height, 0);
    items.push({marker, blocks: innerBlocks, height: h});
    totalHeight += h;
  });
  const ITEM_GAP = 6;
  const totalWithGaps =
      totalHeight + Math.max(0, items.length - 1) * ITEM_GAP;
  return {
    height: totalWithGaps + PARAGRAPH_SPACING / 2,
    trailingSpace: PARAGRAPH_SPACING / 2,
    paint: (g, x, y, _w) => {
      let cy = y;
      for (let idx = 0; idx < items.length; idx++) {
        const item = items[idx]!;
        // Draw marker on the first line of the item.
        g.font = ordered ?
            `500 ${opts.baseSize}px ${FONT_FAMILY}` :
            `${opts.baseSize + 2}px ${FONT_FAMILY}`;
        g.fillStyle = TEXT_MUTED;
        g.textBaseline = 'alphabetic';
        g.fillText(item.marker,
                   x + INDENT - MARKER_GAP -
                       g.measureText(item.marker).width,
                   cy + opts.baseSize);
        let inner = cy;
        for (const b of item.blocks) {
          b.paint(g, x + INDENT, inner, opts.maxWidth - INDENT);
          inner += b.height;
        }
        cy += item.height + (idx < items.length - 1 ? ITEM_GAP : 0);
      }
    },
  };
}

// --------------- canvas primitives ---------------

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

function strokeRoundedRect(
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
  ctx.stroke();
}

// --------------- public entry ---------------

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function lexMarkdown(src: string): any[] {
  try {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const lexer = (marked as any).lexer;
    if (typeof lexer === 'function') {
      const out = lexer.call(marked, src || '');
      if (Array.isArray(out)) return out;
    }
  } catch (e) {
    console.warn('[dao] marked.lexer failed; falling back to plain text', e);
  }
  // Fallback: treat the whole thing as a single paragraph.
  return [{type: 'paragraph', tokens: [{type: 'text', text: src}]}];
}

export async function renderShareImage(ctx: ShareContext): Promise<Blob> {
  const measureCanvas = createCanvas(SHARE_CANVAS_WIDTH, 16);
  const measure =
      measureCanvas.getContext('2d') as unknown as CanvasRenderingContext2D;

  // --- answer markdown blocks ---
  const answerSize = 18;
  const answerLineHeight = Math.round(answerSize * 1.6);
  const answerMaxWidth = SHARE_CANVAS_WIDTH - OUTER_PADDING * 2;
  const answerTokens = lexMarkdown(ctx.answer || '—');
  const answerBlocks = buildBlocks(answerTokens, {
    ctx: measure,
    maxWidth: answerMaxWidth,
    textColor: TEXT_COLOR,
    baseSize: answerSize,
    baseLineHeight: answerLineHeight,
  });
  const answerHeight = sumBlockHeight(answerBlocks);

  // --- question bubble ---
  const bubbleSize = 18;
  const bubbleLineHeight = Math.round(bubbleSize * 1.5);
  const bubbleInnerMax = BUBBLE_MAX_WIDTH - BUBBLE_PADDING_X * 2;
  const questionText = (ctx.question || '').trim();
  const hasBubble = questionText.length > 0;
  const bubbleTokens = hasBubble ? lexMarkdown(questionText) : [];
  const bubbleBlocks = hasBubble ?
      buildBlocks(bubbleTokens, {
        ctx: measure,
        maxWidth: bubbleInnerMax,
        textColor: '#ffffff',
        baseSize: bubbleSize,
        baseLineHeight: bubbleLineHeight,
      }) :
      [];
  // For the bubble width, run a quick text-only sniff to find the widest
  // line. We use the original questionText paragraphs because the block
  // builder collapses runs and we'd rather over-estimate the bubble than
  // wrap mid-glyph.
  let bubbleInnerWidth = 0;
  if (hasBubble) {
    measure.font = `${bubbleSize}px ${FONT_FAMILY}`;
    for (const line of questionText.split('\n')) {
      const w = measure.measureText(line).width;
      if (w > bubbleInnerWidth) bubbleInnerWidth = w;
    }
    bubbleInnerWidth = Math.min(bubbleInnerMax, bubbleInnerWidth);
  }
  const bubbleOuterWidth =
      hasBubble ? bubbleInnerWidth + BUBBLE_PADDING_X * 2 : 0;
  const bubbleInnerHeight = sumBlockHeight(bubbleBlocks);
  const bubbleOuterHeight =
      hasBubble ? bubbleInnerHeight + BUBBLE_PADDING_Y * 2 : 0;

  // --- vertical layout ---
  let y = OUTER_PADDING;
  const bubbleX =
      SHARE_CANVAS_WIDTH - OUTER_PADDING - bubbleOuterWidth;
  const bubbleY = y;
  if (hasBubble) {
    y += bubbleOuterHeight;
    if (ctx.source) {
      y += GAP_BUBBLE_TO_SOURCE;
      y += SOURCE_LINE_HEIGHT;
      y += GAP_SOURCE_TO_ANSWER;
    } else {
      y += GAP_BUBBLE_TO_ANSWER;
    }
  } else if (ctx.source) {
    y += SOURCE_LINE_HEIGHT;
    y += GAP_SOURCE_TO_ANSWER;
  }

  const answerY = y;
  y += answerHeight;
  y += OUTER_PADDING;

  const contentHeight = y;
  const totalHeight =
      Math.min(contentHeight + FOOTER_HEIGHT, SHARE_CANVAS_MAX_HEIGHT);
  const truncated = contentHeight + FOOTER_HEIGHT > SHARE_CANVAS_MAX_HEIGHT;

  // --- paint pass ---
  const dpr = globalThis.devicePixelRatio || 1;
  const canvas =
      createCanvas(SHARE_CANVAS_WIDTH * dpr, totalHeight * dpr);
  const g =
      canvas.getContext('2d') as unknown as CanvasRenderingContext2D;
  g.scale(dpr, dpr);

  g.fillStyle = BG_COLOR;
  g.fillRect(0, 0, SHARE_CANVAS_WIDTH, totalHeight);

  if (hasBubble) {
    g.fillStyle = ACCENT_COLOR;
    paintRoundedRect(
        g, bubbleX, bubbleY, bubbleOuterWidth, bubbleOuterHeight,
        BUBBLE_RADIUS);
    const bx = bubbleX + BUBBLE_PADDING_X;
    let by = bubbleY + BUBBLE_PADDING_Y;
    for (const b of bubbleBlocks) {
      b.paint(g, bx, by, bubbleInnerWidth);
      by += b.height;
    }
  }

  if (ctx.source) {
    g.font = `${SOURCE_FONT_SIZE}px ${FONT_FAMILY}`;
    g.fillStyle = TEXT_VERY_MUTED;
    g.textBaseline = 'alphabetic';
    const sourceText = `From ${ctx.source.domain}`;
    const w = g.measureText(sourceText).width;
    const sourceY = hasBubble ?
        bubbleY + bubbleOuterHeight + GAP_BUBBLE_TO_SOURCE + SOURCE_FONT_SIZE :
        OUTER_PADDING + SOURCE_FONT_SIZE;
    g.fillText(
        sourceText, SHARE_CANVAS_WIDTH - OUTER_PADDING - w, sourceY);
  }

  const answerPaintMax =
      truncated ? totalHeight - FOOTER_HEIGHT - OVERFLOW_FADE_HEIGHT / 2 :
                  totalHeight - FOOTER_HEIGHT - OUTER_PADDING;
  g.save();
  g.beginPath();
  g.rect(
      OUTER_PADDING, answerY, answerMaxWidth, answerPaintMax - answerY);
  g.clip();
  let cy = answerY;
  for (const b of answerBlocks) {
    b.paint(g, OUTER_PADDING, cy, answerMaxWidth);
    cy += b.height;
  }
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
    g.fillStyle = TEXT_VERY_MUTED;
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
