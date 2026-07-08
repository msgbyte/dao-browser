// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Single import point for agent_bridge.ts. Keeping the public surface
// in one file makes it easy to swap the orchestrator if we ever
// introduce a different chain (e.g. user-supplied API keys).

export {webSearch, fetchUrl, getSearchSourceOverride,
        setSearchSourceOverride} from './service.js';
export {isProviderSearchAvailable, getProviderToolSpecKind}
    from './provider_capabilities.js';
export {getJinaApiKey, setJinaApiKey} from './tier_jina.js';
export type {SearchResponse, FetchResponse, FetchSource, SearchResult,
             SearchSource, SearchSourceOverride} from './types.js';
