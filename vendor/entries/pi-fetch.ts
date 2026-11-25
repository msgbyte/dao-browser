// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Injected into the SDK bundle only. Other WebUIs use the platform fetch.
let providerFetch: typeof globalThis.fetch | undefined;
export function setProviderFetch(transport: typeof globalThis.fetch) {
  providerFetch = transport;
}
export const fetch: typeof globalThis.fetch = (input, init) =>
    (providerFetch ?? globalThis.fetch)(input, init);
