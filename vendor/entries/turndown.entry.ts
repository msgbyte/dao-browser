// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// esbuild bundles this entry as an IIFE with `globalName=__VendorModule`.
// Only the TurndownService constructor is needed by the wrapper template,
// so we re-export it.
//
// The produced IIFE is substituted into turndown.tpl.ts at __BUNDLE__. The
// outer wrapper exposes a factory that the page-capture pipeline uses to
// convert a Readability-extracted HTML blob to markdown, with a custom
// image rule that replaces <img> with `[image: <alt>]` so the model sees a
// short token instead of a raw data URL / long image reference.

// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import TurndownService from "turndown";

export { TurndownService };
