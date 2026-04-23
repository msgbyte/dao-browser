// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// esbuild bundles this entry as an IIFE with `globalName=__VendorModule`.
// The only thing we need exposed on the global is the Readability class,
// so we re-export it on the global via a side-effect assignment.
//
// The produced IIFE bundle is then substituted into readability.tpl.ts at
// the __BUNDLE__ placeholder. After substitution, the outer wrapper script
// clones the document, invokes Readability, and returns a JSON-encoded
// article payload — matching the behavior of the legacy
// scripts/gen_readability_bundle.ts.

// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import { Readability } from "@mozilla/readability";

// Expose on the IIFE's returned module object so the wrapper template can
// reach it as `__VendorModule.Readability`.
export { Readability };
