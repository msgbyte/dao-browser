// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  copyPngBlobToClipboard,
  renderDreamReportShareImage,
  renderShareImage,
} from '../dao_share_image.js';

type PaintOp = {
  type: string;
  fillStyle?: unknown;
  strokeStyle?: unknown;
  text?: string;
  x?: number;
  y?: number;
  w?: number;
  h?: number;
};

class FakeGradient {
  stops: Array<{offset: number, color: string}> = [];

  addColorStop(offset: number, color: string) {
    this.stops.push({offset, color});
  }
}

class FakeCanvasContext {
  fillStyle: unknown = '#000';
  strokeStyle: unknown = '#000';
  lineWidth = 1;
  font = '16px sans-serif';
  textAlign = 'left';
  textBaseline = 'alphabetic';

  constructor(private readonly canvas: FakeOffscreenCanvas) {}

  scale() {}
  save() {}
  restore() {}
  beginPath() {}
  closePath() {}
  clip() {}
  quadraticCurveTo() {}
  rect(x: number, y: number, w: number, h: number) {
    this.canvas.ops.push({type: 'rect', x, y, w, h});
  }
  moveTo(x: number, y: number) {
    this.canvas.ops.push({type: 'moveTo', x, y});
  }
  lineTo(x: number, y: number) {
    this.canvas.ops.push({type: 'lineTo', x, y});
  }
  fill() {
    this.canvas.ops.push({type: 'fill', fillStyle: this.fillStyle});
  }
  stroke() {
    this.canvas.ops.push({type: 'stroke', strokeStyle: this.strokeStyle});
  }
  fillRect(x: number, y: number, w: number, h: number) {
    this.canvas.ops.push({
      type: 'fillRect',
      fillStyle: this.fillStyle,
      x,
      y,
      w,
      h,
    });
  }
  fillText(text: string, x: number, y: number) {
    this.canvas.ops.push({
      type: 'fillText',
      fillStyle: this.fillStyle,
      text,
      x,
      y,
    });
  }
  createLinearGradient() {
    return new FakeGradient();
  }
  measureText(text: string) {
    const sizeMatch = /(\d+(?:\.\d+)?)px/.exec(this.font);
    const size = sizeMatch ? Number(sizeMatch[1]) : 16;
    return {width: Array.from(text).length * size * 0.58};
  }
}

class FakeOffscreenCanvas {
  ops: PaintOp[] = [];

  constructor(readonly width: number, readonly height: number) {}

  getContext() {
    return new FakeCanvasContext(this);
  }

  convertToBlob() {
    return Promise.resolve(new Blob([
      JSON.stringify({width: this.width, height: this.height, ops: this.ops}),
    ], {type: 'application/json'}));
  }
}

async function renderOps(markdown: string, dark = false): Promise<PaintOp[]> {
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

  const blob = await renderShareImage({
    question: '写一下',
    answer: markdown,
  });
  return JSON.parse(await blob.text()).ops;
}

async function renderDreamOps(markdown: string, dark = false): Promise<PaintOp[]> {
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

describe('renderShareImage visual parity', () => {
  const originalOffscreenCanvas = globalThis.OffscreenCanvas;
  const originalDevicePixelRatio = globalThis.devicePixelRatio;
  const originalMatchMedia = window.matchMedia;
  const originalClipboardItemDescriptor =
      Object.getOwnPropertyDescriptor(window, 'ClipboardItem');
  const originalClipboardDescriptor =
      Object.getOwnPropertyDescriptor(navigator, 'clipboard');

  function restorePropertyDescriptor(
      target: object, key: PropertyKey,
      descriptor: PropertyDescriptor | undefined) {
    if (descriptor) {
      Object.defineProperty(target, key, descriptor);
      return;
    }
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    delete (target as any)[key];
  }

  beforeEach(() => {
    Object.defineProperty(globalThis, 'OffscreenCanvas', {
      configurable: true,
      value: FakeOffscreenCanvas,
    });
    Object.defineProperty(globalThis, 'devicePixelRatio', {
      configurable: true,
      value: 1,
    });
  });

  afterEach(() => {
    Object.defineProperty(globalThis, 'OffscreenCanvas', {
      configurable: true,
      value: originalOffscreenCanvas,
    });
    Object.defineProperty(globalThis, 'devicePixelRatio', {
      configurable: true,
      value: originalDevicePixelRatio,
    });
    Object.defineProperty(window, 'matchMedia', {
      configurable: true,
      value: originalMatchMedia,
    });
    restorePropertyDescriptor(
        window, 'ClipboardItem', originalClipboardItemDescriptor);
    restorePropertyDescriptor(
        navigator, 'clipboard', originalClipboardDescriptor);
  });

  it('uses the dark sidebar palette when the agent page is in dark mode', async () => {
    const ops = await renderOps('正文', true);
    expect(ops.find(op => op.type === 'fillRect')?.fillStyle)
        .toBe('rgb(54, 59, 64)');
  });

  it('does not draw markdown heading separator rules', async () => {
    const ops = await renderOps('## 问题抓得很准\n\n正文');
    expect(ops.filter(op => op.type === 'stroke')).toHaveLength(0);
  });

  it('keeps one compact paragraph gap for markdown blank lines', async () => {
    const ops = await renderOps('第一段\n\n第二段');
    const first = ops.find(op => op.type === 'fillText' && op.text === '第一段');
    const second =
        ops.find(op => op.type === 'fillText' && op.text === '第二段');
    expect((second!.y ?? 0) - (first!.y ?? 0)).toBeLessThanOrEqual(45);
  });

  it('paints blockquotes with the dark sidebar quote bar color', async () => {
    const ops = await renderOps('> DAU 1w 不仅依赖留存', true);
    expect(ops.find(op => op.type === 'fillRect' && op.w === 4)?.fillStyle)
        .toBe('rgba(255, 255, 255, 0.16)');
  });

  it('renders markdown table cells instead of the table placeholder', async () => {
    const ops = await renderOps(
        '| Product | Users |\n| --- | ---: |\n| Dao | 1200 |');
    const texts = ops.filter(op => op.type === 'fillText').map(op => op.text);
    expect(texts).toContain('Product');
    expect(texts).toContain('Users');
    expect(texts).toContain('Dao');
    expect(texts).toContain('1200');
    expect(texts).not.toContain('[table]');
  });

  it('renders dream report share images with title date body and dream footer',
     async () => {
       const ops = await renderDreamOps('## Focus\n\n- Read specs\n- Ship UI');
       const texts =
           ops.filter(op => op.type === 'fillText').map(op => op.text);
       const joined = texts.join('');

       expect(joined).toContain('Dream Report');
       expect(joined).toContain('About your day on 2026-06-19');
       expect(joined).toContain('Focus');
       expect(joined).toContain('Read');
       expect(joined).toContain('Ship UI');
       expect(joined).toContain('Dreamed by Dao Browser');
       expect(texts).not.toContain('Answered by Dao Browser');

       let cursor = -1;
       const expectedInOrder = ['Dream Report', 'Focus', 'Read', 'Ship UI'];
       for (const text of expectedInOrder) {
         const idx = joined.indexOf(text, cursor + 1);
         expect(idx).toBeGreaterThan(cursor);
         cursor = idx;
       }
     });

  it('does not draw a chat user bubble for dream report share images',
     async () => {
       const ops = await renderDreamOps('Report body');
       const bubbleRects =
           ops.filter(op => op.type === 'fill' && op.fillStyle === 'rgb(70, 120, 190)');
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

  it('rejects png copy when the clipboard write API is unavailable', async () => {
    Object.defineProperty(window, 'ClipboardItem', {
      configurable: true,
      value: FakeClipboardItem,
    });
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {},
    });

    await expect(copyPngBlobToClipboard(new Blob(['png'])))
        .rejects.toThrow('ClipboardItem API unavailable');
  });
});
