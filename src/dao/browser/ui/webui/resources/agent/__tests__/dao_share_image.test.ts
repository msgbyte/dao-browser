// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it} from 'vitest';

import {renderShareImage} from '../dao_share_image.js';

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

describe('renderShareImage visual parity', () => {
  const originalOffscreenCanvas = globalThis.OffscreenCanvas;
  const originalDevicePixelRatio = globalThis.devicePixelRatio;
  const originalMatchMedia = window.matchMedia;

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
});
