// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This entry point has been superseded by the generic vendor pipeline.
// `npm run gen:readability` now delegates to `npm run vendor -- --entry=readability`,
// which reads vendor.config.ts and builds readability_bundle.ts through the
// unified bundler. See vendor/README.md for the contract.

import { spawnSync } from "node:child_process";

const result = spawnSync(
  "npx",
  ["tsx", "scripts/cli.ts", "vendor", "--entry=readability"],
  { stdio: "inherit" }
);
process.exit(result.status ?? 1);
