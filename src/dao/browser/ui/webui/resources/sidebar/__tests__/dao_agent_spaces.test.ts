// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

import type {AgentSpaceData, AgentSpacesState} from '../dao_agent_spaces.js';
import {SIDEBAR_POINTER_EXITED_EVENT} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {
    getString: (id: string) => `[${id}]`,
    getStringF: (id: string, value: string) => `[${id}:${value}]`,
  },
}));

interface TestAgentSpaces extends HTMLElement {
  updateComplete: Promise<boolean>;
}

function space(extra: Partial<AgentSpaceData> = {}): AgentSpaceData {
  return {
    space_id: 's1-abcd',
    name: 'Compare prices',
    state: 'running',
    owner: 'Dao Agent',
    status: '',
    created_at: 0,
    tab_count: 1,
    window_visible: false,
    events: [],
    ...extra,
  };
}

function push(event: string, payload: unknown) {
  (window as unknown as {
    cr: {webUIListenerCallback: (event: string, ...args: unknown[]) => void},
  }).cr.webUIListenerCallback(event, payload);
}

async function createElement(state: AgentSpacesState) {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  const el = document.createElement('dao-agent-spaces') as TestAgentSpaces;
  document.body.appendChild(el);
  await el.updateComplete;
  push('agentSpacesChanged', state);
  await el.updateComplete;
  return {el, send};
}

function click(target: Element) {
  target.dispatchEvent(
      new MouseEvent('click', {bubbles: true, cancelable: true, composed: true}));
}

describe('dao-agent-spaces', () => {
  beforeAll(async () => {
    await import('../dao_agent_spaces.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.useRealTimers();
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('requests state and renders nothing without tasks', async () => {
    const {el, send} =
        await createElement({spaces: [], currentSpaceId: ''});

    expect(send).toHaveBeenCalledWith('getAgentSpaces', []);
    expect(el.shadowRoot!.querySelector('.row')).toBeNull();
  });

  it('lists tasks with their latest step and asks for the user', async () => {
    const {el} = await createElement({
      spaces: [
        space({
          events: [{
            time: 0,
            tool: 'click_by_ref',
            label: 'Open the cart',
            url: 'https://shop.example/',
            pending: true,
            ok: false,
          }],
        }),
        space({
          space_id: 's2-ef01',
          name: 'Book a table',
          state: 'user_in_control',
          handoff_reason: 'help_requested',
          handoff_message: 'Sign in',
        }),
      ],
      currentSpaceId: '',
    });

    const details = Array.from(el.shadowRoot!.querySelectorAll('.detail'))
                        .map(node => node.textContent!.trim());
    expect(details).toEqual([
      'Open the cart',
      '[daoAgentSpaceNeedsYou:Sign in]',
    ]);
  });

  it('explains why Dao stopped an inactive task', async () => {
    const {el} = await createElement({
      spaces: [
        space({state: 'stopped', stop_reason: 'inactive'}),
        space({space_id: 's2-ef01', state: 'stopped'}),
      ],
      currentSpaceId: '',
    });

    const details = Array.from(el.shadowRoot!.querySelectorAll('.detail'))
                        .map(node => node.textContent!.trim());
    expect(details).toEqual([
      '[daoAgentSpaceStateStoppedInactive]',
      '[daoAgentSpaceStateStopped]',
    ]);
  });

  it('stops a task without opening its panel', async () => {
    const {el, send} =
        await createElement({spaces: [space()], currentSpaceId: ''});
    const stop = el.shadowRoot!.querySelectorAll('.icon-btn')[1]!;

    click(stop);

    expect(send).toHaveBeenCalledWith('agentSpaceAction', ['stop', 's1-abcd']);
    expect(send).not.toHaveBeenCalledWith('startAgentSpaceStream',
                                          ['s1-abcd']);
  });

  it('streams the task while its panel is open', async () => {
    const {el, send} =
        await createElement({spaces: [space()], currentSpaceId: ''});

    click(el.shadowRoot!.querySelector('.row')!);
    await el.updateComplete;
    expect(send).toHaveBeenCalledWith('startAgentSpaceStream', ['s1-abcd']);

    push('agentSpaceFrame', {spaceId: 's1-abcd', dataUrl: 'data:image/jpeg;x'});
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.frame img')!.getAttribute('src'))
        .toBe('data:image/jpeg;x');

    click(el.shadowRoot!.querySelector('.row')!);
    await el.updateComplete;
    expect(send).toHaveBeenCalledWith('stopAgentSpaceStream', []);
  });

  it.each(['pointerleave', 'native exit', 'disconnect', 'removed', 'finished',
           'stopped'])(
      'cancels pending and active hover capture on %s', async reason => {
        vi.useFakeTimers();
        for (const delay of [0, 300]) {
          const {el, send} = await createElement({
            spaces: [space()], currentSpaceId: '',
          });
          const row = el.shadowRoot!.querySelector('.row')!;
          row.dispatchEvent(new MouseEvent('pointerenter'));
          vi.advanceTimersByTime(delay);
          if (delay) {
            expect(send).toHaveBeenCalledWith(
                'showAgentSpacePreview', [1032, 0, 's1-abcd']);
          }
          send.mockClear();

          if (reason === 'pointerleave') {
            row.dispatchEvent(new MouseEvent('pointerleave'));
          } else if (reason === 'native exit') {
            window.dispatchEvent(new Event(SIDEBAR_POINTER_EXITED_EVENT));
          } else if (reason === 'disconnect') {
            el.remove();
          } else {
            push('agentSpacesChanged', {
              spaces: reason === 'removed' ? [] : [space({
                state: reason as 'finished'|'stopped',
              })],
              currentSpaceId: '',
            });
          }
          await el.updateComplete;
          vi.advanceTimersByTime(1000);

          expect(send).not.toHaveBeenCalledWith(
              'showAgentSpacePreview', expect.anything());
          if (delay) {
            expect(send).toHaveBeenCalledWith('hideAgentSpacePreview', []);
          }
          el.remove();
        }
      });

  it('counts running tasks in the section header', async () => {
    const {el} = await createElement({
      spaces: [
        space(),
        space({space_id: 's2-ef01', state: 'user_in_control'}),
        space({space_id: 's3-2345', state: 'finished', summary: 'Done'}),
      ],
      currentSpaceId: '',
    });

    expect(el.shadowRoot!.querySelector('.section-header .count')!
               .textContent!.trim())
        .toBe('2');
  });

  it('offers to resolve a step only the user can do', async () => {
    const {el, send} = await createElement({
      spaces: [
        space({
          state: 'user_in_control',
          handoff_reason: 'file_chooser',
        }),
        space({
          space_id: 's2-ef01',
          state: 'user_in_control',
          handoff_reason: 'manual_takeover',
        }),
      ],
      currentSpaceId: '',
    });

    const resolve = el.shadowRoot!.querySelectorAll('.btn.attention');
    expect(resolve).toHaveLength(1);
    expect(el.shadowRoot!.querySelector('.state-icon.attention')).not.toBeNull();
    expect(el.shadowRoot!.querySelector('.state-icon.control')).not.toBeNull();

    click(resolve[0]!);
    expect(send).toHaveBeenCalledWith('agentSpaceAction', ['show', 's1-abcd']);
    expect(send).not.toHaveBeenCalledWith('startAgentSpaceStream',
                                          ['s1-abcd']);
  });

  it('shows the summary of a finished task with a remove button',
     async () => {
       const {el, send} = await createElement({
         spaces: [space({state: 'finished', summary: 'Sony is the best'})],
         currentSpaceId: '',
       });

       const detail = el.shadowRoot!.querySelector('.detail.result')!;
       expect(detail.textContent!.trim()).toBe('Sony is the best');
       expect(el.shadowRoot!.querySelector('.state-icon.finished'))
           .not.toBeNull();

       click(el.shadowRoot!.querySelector('.row-actions.always .icon-btn')!);
       expect(send).toHaveBeenCalledWith('agentSpaceAction',
                                         ['dismiss', 's1-abcd']);
     });

  it('leads the open card with the current status and recent steps',
     async () => {
       const events = ['Open the shop', 'Find the search box', 'Type Pro',
                       'Press search', 'Add to compare']
                          .map((label, i) => ({
                            time: i,
                            tool: 'click_by_ref',
                            label,
                            url: 'https://shop.example/',
                            pending: i === 4,
                            ok: i !== 4,
                          }));
       const {el} = await createElement({
         spaces: [space({status: 'Comparing Pro models', events})],
         currentSpaceId: '',
       });

       click(el.shadowRoot!.querySelector('.row')!);
       await el.updateComplete;

       const root = el.shadowRoot!;
       expect(root.querySelector('.now-text')!.textContent!.trim())
           .toBe('Comparing Pro models');
       expect(root.querySelector('.space.expanded .detail')!.textContent!
                  .trim())
           .toBe('[daoAgentSpaceStartedBy:Dao Agent]');
       const steps = () => Array.from(root.querySelectorAll('.step'))
                               .map(node => node.textContent!.trim());
       expect(steps()).toEqual(['Add to compare', 'Press search', 'Type Pro']);
       expect(root.querySelector('.step.pending')!.textContent!.trim())
           .toBe('Add to compare');

       click(root.querySelector('.link-btn')!);
       await el.updateComplete;
       expect(steps()).toHaveLength(5);
       expect(root.querySelector('.link-btn')!.textContent!.trim())
           .toBe('[daoAgentSpaceShowFewerSteps]');
     });

  it('does not repeat the newest step without a status', async () => {
    const {el} = await createElement({
      spaces: [space({
        events: [{
          time: 0,
          tool: 'click_by_ref',
          label: 'Open the cart',
          url: 'https://shop.example/',
          pending: true,
          ok: false,
        }],
      })],
      currentSpaceId: '',
    });

    click(el.shadowRoot!.querySelector('.row')!);
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.now-text')!.textContent!.trim())
        .toBe('Open the cart');
    expect(el.shadowRoot!.querySelector('.timeline')).toBeNull();
  });

  it('shows hand back in the task window while the user has control',
     async () => {
       const {el, send} = await createElement({
         spaces: [space({state: 'user_in_control'})],
         currentSpaceId: 's1-abcd',
       });

       expect(el.shadowRoot!.querySelector('.bar')).not.toBeNull();
       const handBack = el.shadowRoot!.querySelector('.btn.primary')!;
       expect(handBack.textContent).toContain('[daoAgentSpaceHandBack]');

       click(handBack);
       expect(send).toHaveBeenCalledWith('agentSpaceAction',
                                         ['hand_back', 's1-abcd']);
     });
});
