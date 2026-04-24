// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Declarative configuration for the `npm run vendor` pipeline.
//
// The pipeline downloads pinned npm packages (versions in vendor/package.json),
// bundles them with esbuild (or a customBuild script), and writes the output
// into the Chromium WebUI resource tree. Generated artifacts are committed;
// node_modules is not.

export type VendorFormat = "esm" | "iife-string" | "customBuild";

export interface VendorEntry {
  name: string;
  npm: string[];
  format: VendorFormat;

  // For esm / iife-string: path relative to repo root.
  entry?: string;
  // For iife-string only: template file that wraps the minified bundle.
  wrapperTemplate?: string;
  // For customBuild only: path to the build script (invoked with node).
  customBuild?: string;

  outDir: string;
  outName: string;

  target?: string;
  minify?: boolean;
  sourcemap?: boolean;

  // ESM only: modules to mark as external (not bundled). Used for node-only
  // optional deps that browser-target vendored packages guard with runtime
  // `typeof process !== "undefined"` checks — esbuild still tries to resolve
  // them unless explicitly excluded. These externals must never be imported
  // at runtime; they would throw an unresolvable-specifier error in the
  // browser.
  external?: string[];

  // How the .d.ts file is produced alongside an ESM bundle:
  //  - "handwritten": developer maintains <outName>.d.ts in outDir
  //  - "none": no .d.ts
  dts?: "handwritten" | "none";
}

export interface VendorConfig {
  entries: VendorEntry[];
}

const config: VendorConfig = {
  entries: [
    {
      name: "readability",
      npm: ["@mozilla/readability"],
      format: "iife-string",
      entry: "vendor/entries/readability.entry.ts",
      wrapperTemplate: "vendor/entries/readability.tpl.ts",
      outDir: "src/dao/browser/ui/webui/resources/agent",
      outName: "readability_bundle.ts",
      target: "es2022",
      minify: true,
    },
    {
      // HTML -> Markdown converter. Paired with readability: Readability
      // extracts the article HTML, Turndown converts it to markdown, and
      // the combined script is injected via DevTools Runtime.evaluate to
      // capture the active tab's page as a markdown block attached to the
      // user's next agent message.
      name: "turndown",
      npm: ["turndown"],
      format: "iife-string",
      entry: "vendor/entries/turndown.entry.ts",
      wrapperTemplate: "vendor/entries/turndown.tpl.ts",
      outDir: "src/dao/browser/ui/webui/resources/agent",
      outName: "turndown_bundle.ts",
      target: "es2022",
      minify: true,
    },
    {
      // Unified pi-mono runtime: pi-ai + pi-agent-core + pi-web-ui all in
      // one ESM bundle so downstream consumers share a single JavaScript
      // module instance (and we avoid the ~1.8 MB duplication that
      // appeared when pi-web-ui's dep closure pulled its own copy of
      // pi-ai/pi-agent-core/typebox).
      //
      // Emits a `.ts` file (not `.js`) so it can flow through Chromium's
      // build_webui pipeline via `non_web_component_files`. The pipeline
      // prepends `@ts-nocheck` to skip type-checking the minified bundle.
      name: "pi-runtime",
      npm: [
        "@mariozechner/pi-ai",
        "@mariozechner/pi-agent-core",
        "@mariozechner/pi-web-ui",
        "@mariozechner/mini-lit",
        "@sinclair/typebox",
        "lit",
      ],
      format: "esm",
      entry: "vendor/entries/pi-runtime.entry.ts",
      outDir: "src/dao/browser/ui/webui/resources/agent/vendor",
      outName: "pi_runtime_bundle.ts",
      target: "es2022",
      minify: true,
      sourcemap: false,
      dts: "none",
      external: [
        // Heavy pi-web-ui optional preview deps unused by the Dao agent.
        // Any pi-web-ui component that touches these at runtime must be
        // stubbed out before use.
        "pdfjs-dist",
        "pdfjs-dist/*",
        "xlsx",
        "docx-preview",
        "jszip",
        // Local LLM SDKs — the Dao agent routes through pi-ai's own
        // provider tree, so these are dead weight in our bundle. Stubbed
        // to an empty namespace by the vendor pipeline so no bare
        // specifier leaks into the emitted ESM.
        "@lmstudio/sdk",
        "ollama",
        "ollama/browser",
      ],
    },
    {
      // Copies pi-web-ui's pre-compiled Tailwind v4 CSS verbatim into the
      // agent resource tree. No Tailwind JIT pass — the upstream package
      // already publishes the final stylesheet.
      name: "pi-web-ui-css",
      npm: ["@mariozechner/pi-web-ui"],
      format: "customBuild",
      customBuild: "vendor/entries/pi-web-ui-css.build.mjs",
      outDir: "src/dao/browser/ui/webui/resources/agent/vendor",
      outName: "pi_web_ui.css",
    },
  ],
};

export default config;
