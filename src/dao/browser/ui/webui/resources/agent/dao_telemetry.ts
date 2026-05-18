// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Lightweight client for the Tianji application tracking endpoint. Mirrors the
// HTTP shape of `tianji-client-sdk` (POST /api/application/send with a
// {type, payload} envelope) so the same Tianji application can receive both
// C++-side `open` events and WebUI-side agent events.
//
// All errors are swallowed — telemetry must never affect user-facing
// behavior. See dao_telemetry_service.cc for the C++ counterpart.

const TIANJI_ENDPOINT = 'https://app.tianji.dev/api/application/send';
const TIANJI_APPLICATION_ID = 'cmpb4zdf0he9p78rcrkg6j7vg';

type EventData = Record<string, unknown>;

export function reportTelemetryEvent(
    eventName: string, eventData?: EventData): void {
  const envelope = {
    type: 'event',
    payload: {
      application: TIANJI_APPLICATION_ID,
      name: eventName,
      data: eventData ?? {},
    },
  };

  try {
    void fetch(TIANJI_ENDPOINT, {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(envelope),
      credentials: 'omit',
      keepalive: true,
    }).catch(() => {});
  } catch {
    // ignore
  }
}
