// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

import type {MediaPlaybackState} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestMediaControl extends HTMLElement {
  mediaState_: MediaPlaybackState | null;
  updateComplete: Promise<boolean>;
}

function mediaState(extra: Partial<MediaPlaybackState> = {}):
    MediaPlaybackState {
  return {
    isPlaying: true,
    canPlayPause: true,
    tabIndex: 3,
    title: 'Video',
    sourceTitle: 'example.com',
    faviconUrl: '',
    isMuted: false,
    hasPrev: false,
    hasNext: false,
    ...extra,
  };
}

async function createMediaControl() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};

  const el = document.createElement('dao-media-control') as TestMediaControl;
  el.mediaState_ = mediaState();
  document.body.appendChild(el);
  await el.updateComplete;
  return {el, send};
}

describe('dao-media-control', () => {
  beforeAll(async () => {
    await import('../dao_media_control.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('renders the picture-in-picture button before dismiss', async () => {
    const {el} = await createMediaControl();

    const headerButtons =
        Array.from(el.shadowRoot!.querySelectorAll('.media-header button'));

    expect(headerButtons).toHaveLength(2);
    expect(headerButtons[0]!.classList.contains('pip-btn')).toBe(true);
    expect(headerButtons[1]!.classList.contains('dismiss-btn')).toBe(true);
  });

  it('keeps muted media visible while playback control is unavailable',
      async () => {
        const {el, send} = await createMediaControl();
        el.mediaState_ = mediaState({isMuted: true, canPlayPause: false});
        await el.updateComplete;

        let button =
            el.shadowRoot!.querySelector<HTMLButtonElement>('.play-pause')!;
        expect(el.shadowRoot!.querySelector('.media-card')).not.toBeNull();
        expect(button.disabled).toBe(true);
        button.click();
        expect(send).not.toHaveBeenCalled();

        el.mediaState_ = mediaState();
        await el.updateComplete;
        button =
            el.shadowRoot!.querySelector<HTMLButtonElement>('.play-pause')!;
        expect(button.disabled).toBe(false);
        button.click();
        expect(send).toHaveBeenCalledWith('mediaPlayPause', []);
      });

  it('requests picture-in-picture without bubbling to tab activation',
      async () => {
        const {el, send} = await createMediaControl();
        const click = new MouseEvent('click', {
          bubbles: true,
          cancelable: true,
          composed: true,
        });
        const cardClick = vi.fn();
        el.shadowRoot!.querySelector('.media-card')!
            .addEventListener('click', cardClick);

        el.shadowRoot!.querySelector<HTMLButtonElement>('.pip-btn')!
            .dispatchEvent(click);

        expect(send).toHaveBeenCalledWith('mediaPictureInPicture', []);
        expect(cardClick).not.toHaveBeenCalled();
      });
});
