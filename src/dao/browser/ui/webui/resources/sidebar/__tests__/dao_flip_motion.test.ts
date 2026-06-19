// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from '../dao_flip_motion.js';

const originalAnimateDescriptor =
    Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'animate');
const originalMatchMediaDescriptor =
    Object.getOwnPropertyDescriptor(window, 'matchMedia');

function rect(left: number, top: number, width = 100, height = 36): DOMRect {
  return {
    left,
    top,
    width,
    height,
    right: left + width,
    bottom: top + height,
    x: left,
    y: top,
    toJSON: () => ({}),
  } as DOMRect;
}

function setRect(element: HTMLElement, bounds: DOMRect) {
  element.getBoundingClientRect = () => bounds;
}

function installAnimateMock() {
  const animate = vi.fn(() => ({
    cancel: vi.fn(),
    finished: Promise.resolve(),
  } as unknown as Animation));
  Object.defineProperty(HTMLElement.prototype, 'animate', {
    configurable: true,
    value: animate,
  });
  return animate;
}

function restoreDescriptor(
    target: object, key: PropertyKey, descriptor?: PropertyDescriptor) {
  if (descriptor) {
    Object.defineProperty(target, key, descriptor);
    return;
  }

  Reflect.deleteProperty(target, key);
}

describe('dao_flip_motion', () => {
  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    restoreDescriptor(
        HTMLElement.prototype, 'animate', originalAnimateDescriptor);
    restoreDescriptor(window, 'matchMedia', originalMatchMediaDescriptor);
  });

  it('captures visible elements by stable identity', () => {
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="b"></div>
        <div class="item" data-id="hidden"></div>
        <div class="item" data-id="invisible" style="visibility: hidden"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    setRect(items[2]!, rect(0, 80, 0, 0));
    setRect(items[3]!, rect(0, 120));

    const snapshot = snapshotFlipElements(
        root, '.item', element => element.dataset.id || '');

    expect([...snapshot.ids]).toEqual(['a', 'b']);
    expect(snapshot.rects.get('b')!.top).toBe(40);
    expect(snapshot.ids.has('hidden')).toBe(false);
    expect(snapshot.rects.has('hidden')).toBe(false);
    expect(snapshot.ids.has('invisible')).toBe(false);
    expect(snapshot.rects.has('invisible')).toBe(false);
  });

  it('animates surviving elements when an identity was removed', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(1);
    expect(animate.mock.contexts[0]).toBe(items[1]);
    expect(animate).toHaveBeenCalledWith(
        [
          {transform: 'translate(0px, 40px)'},
          {transform: 'translate(0, 0)'},
        ],
        {
          duration: 140,
          easing: 'cubic-bezier(0.2, 0, 0, 1)',
        });
  });

  it('cancels only earlier animations created by this helper', () => {
    const animate = installAnimateMock();
    const unrelatedCancel = vi.fn();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    items[1]!.getAnimations = () => [
      {cancel: unrelatedCancel} as unknown as Animation,
    ];
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animate).toHaveBeenCalledTimes(1);
    expect(unrelatedCancel).not.toHaveBeenCalled();
  });

  it('cancels the previous helper animation for the same element', () => {
    installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    const firstAnimations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');
    const secondAnimations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(firstAnimations).toHaveLength(1);
    expect(secondAnimations).toHaveLength(1);
    expect(firstAnimations[0]!.cancel).toHaveBeenCalledTimes(1);
  });

  it('cancels active helper animation before measuring an interrupted close',
      () => {
        const animations: Animation[] = [];
        let currentTop = 40;
        let animationIndex = 0;
        const animate = vi.fn(() => {
          const cancel = animationIndex === 0 ?
              vi.fn(() => {
                currentTop = 0;
              }) :
              vi.fn();
          const animation = {
            cancel,
            finished: new Promise<never>(() => {}),
          } as unknown as Animation;
          animations.push(animation);
          animationIndex++;
          return animation;
        });
        Object.defineProperty(HTMLElement.prototype, 'animate', {
          configurable: true,
          value: animate,
        });
        document.body.innerHTML = `
          <div id="root">
            <div class="item" data-id="c"></div>
          </div>
        `;
        const root = document.querySelector('#root')!;
        const item = root.querySelector('.item') as HTMLElement;
        item.getBoundingClientRect = () => rect(0, currentTop);
        const firstPrevious: FlipMotionSnapshot = {
          ids: new Set(['b', 'c']),
          rects: new Map([
            ['b', rect(0, 40)],
            ['c', rect(0, 80)],
          ]),
        };

        animateSurvivingFlipElements(
            firstPrevious, root, '.item',
            element => element.dataset.id || '');

        currentTop = 19;
        animate.mockClear();
        const interruptedPrevious: FlipMotionSnapshot = {
          ids: new Set(['c', 'd']),
          rects: new Map([
            ['c', rect(0, 57)],
            ['d', rect(0, 95)],
          ]),
        };

        animateSurvivingFlipElements(
            interruptedPrevious, root, '.item',
            element => element.dataset.id || '');

        expect(animations[0]!.cancel).toHaveBeenCalledTimes(1);
        expect(animate).toHaveBeenCalledWith(
            [
              {transform: 'translate(0px, 57px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
      });

  it('does not cancel active helper animation for updates without removal',
      () => {
        const animations: Animation[] = [];
        let currentTop = 40;
        const animate = vi.fn(() => {
          const animation = {
            cancel: vi.fn(() => {
              currentTop = 0;
            }),
            finished: new Promise<never>(() => {}),
          } as unknown as Animation;
          animations.push(animation);
          return animation;
        });
        Object.defineProperty(HTMLElement.prototype, 'animate', {
          configurable: true,
          value: animate,
        });
        document.body.innerHTML = `
          <div id="root">
            <div class="item" data-id="c"></div>
          </div>
        `;
        const root = document.querySelector('#root')!;
        const item = root.querySelector('.item') as HTMLElement;
        item.getBoundingClientRect = () => rect(0, currentTop);
        const firstPrevious: FlipMotionSnapshot = {
          ids: new Set(['b', 'c']),
          rects: new Map([
            ['b', rect(0, 40)],
            ['c', rect(0, 80)],
          ]),
        };

        animateSurvivingFlipElements(
            firstPrevious, root, '.item',
            element => element.dataset.id || '');

        currentTop = 19;
        animate.mockClear();
        const unchangedPrevious: FlipMotionSnapshot = {
          ids: new Set(['c']),
          rects: new Map([
            ['c', rect(0, 57)],
          ]),
        };

        const nextAnimations = animateSurvivingFlipElements(
            unchangedPrevious, root, '.item',
            element => element.dataset.id || '');

        expect(nextAnimations).toHaveLength(0);
        expect(animations[0]!.cancel).not.toHaveBeenCalled();
        expect(animate).not.toHaveBeenCalled();
        expect(currentTop).toBe(19);
      });

  it('does not animate duplicate current identities', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    setRect(items[2]!, rect(0, 120));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(1);
    expect(animate.mock.contexts).toEqual([items[1]]);
  });

  it('does not animate when no identity was removed', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="b"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(0);
    expect(animate).not.toHaveBeenCalled();
  });

  it('animates moved survivors with force when no identity was removed', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="b"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '',
        {force: true});

    expect(animations).toHaveLength(1);
    expect(animate.mock.contexts[0]).toBe(items[1]);
    expect(animate).toHaveBeenCalledWith(
        [
          {transform: 'translate(0px, 40px)'},
          {transform: 'translate(0, 0)'},
        ],
        {
          duration: 140,
          easing: 'cubic-bezier(0.2, 0, 0, 1)',
        });
  });

  it('skips animation when requested by options', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '',
        {skip: true});

    expect(animations).toHaveLength(0);
    expect(animate).not.toHaveBeenCalled();
  });

  it('skips animation when reduced motion is requested', () => {
    const animate = installAnimateMock();
    Object.defineProperty(window, 'matchMedia', {
      configurable: true,
      value: () => ({
        matches: true,
        media: '(prefers-reduced-motion: reduce)',
        onchange: null,
        addListener: vi.fn(),
        removeListener: vi.fn(),
        addEventListener: vi.fn(),
        removeEventListener: vi.fn(),
        dispatchEvent: vi.fn(),
      } as unknown as MediaQueryList),
    });
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const item = root.querySelector('.item') as HTMLElement;
    setRect(item, rect(0, 0));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['gone', 'a']),
      rects: new Map([
        ['gone', rect(0, 0)],
        ['a', rect(0, 40)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(0);
    expect(animate).not.toHaveBeenCalled();
  });
});
