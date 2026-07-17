// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Sole owner of the resident WebUI's dream-run listener. Daily and weekly
// reports share this guard so provider requests cannot overlap.

import {addWebUIListener} from './agent_bridge.js';
import {runDream} from './dao_dream_runner.js';
import {runWeeklyDream} from './dao_weekly_dream_runner.js';

export interface DreamRunPayload {
  requestId: string;
  reportKind: 'daily'|'weekly';
  periodStart: string;
  periodEnd: string;
  material: unknown;
  debug: boolean;
}

type DreamFailureCode = 'configuration'|'provider'|'invalid_output';

let dreamInFlight = false;

function parsePayload(payload: unknown): DreamRunPayload|null {
  if (typeof payload !== 'object' || payload === null) return null;
  const candidate = payload as Record<string, unknown>;
  if (!Object.prototype.hasOwnProperty.call(candidate, 'material') ||
      typeof candidate['requestId'] !== 'string' ||
      candidate['requestId'].length === 0 ||
      (candidate['reportKind'] !== 'daily' &&
       candidate['reportKind'] !== 'weekly') ||
      typeof candidate['periodStart'] !== 'string' ||
      candidate['periodStart'].length === 0 ||
      typeof candidate['periodEnd'] !== 'string' ||
      candidate['periodEnd'].length === 0 ||
      typeof candidate['debug'] !== 'boolean') {
    return null;
  }
  return {
    requestId: candidate['requestId'],
    reportKind: candidate['reportKind'],
    periodStart: candidate['periodStart'],
    periodEnd: candidate['periodEnd'],
    material: candidate['material'],
    debug: candidate['debug'],
  };
}

function classifyFailure(message: string): DreamFailureCode {
  if (message === 'no LLM api key configured' ||
      message === 'no LLM provider configured') {
    return 'configuration';
  }
  if (/^invalid (?:JSON|weekly output) after retry:/i.test(message)) {
    return 'invalid_output';
  }
  return 'provider';
}

async function dispatchDream(payload: DreamRunPayload): Promise<void> {
  dreamInFlight = true;
  try {
    const result = payload.reportKind === 'daily' ?
        await runDream(payload.periodStart, payload.material, {
          debug: payload.debug,
        }) :
        await runWeeklyDream({
          start: payload.periodStart,
          end: payload.periodEnd,
        }, payload.material, {debug: payload.debug});
    if (result === null) {
      chrome.send('dreamComplete', [
        payload.requestId,
        {status: 'skipped'},
      ]);
      return;
    }
    chrome.send('dreamComplete', [
      payload.requestId,
      {status: 'completed', result},
    ]);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    chrome.send('dreamFailed', [
      payload.requestId,
      {code: classifyFailure(message), message},
    ]);
  } finally {
    dreamInFlight = false;
  }
}

addWebUIListener('dream-run', (payload: unknown) => {
  const parsed = parsePayload(payload);
  if (!parsed || dreamInFlight) return;
  void dispatchDream(parsed);
});
