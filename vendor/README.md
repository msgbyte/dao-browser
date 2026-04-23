# vendor/

Isolated npm workspace for dependencies that are bundled into the Chromium
WebUI resource tree. Managed by `npm run vendor` (see
`scripts/commands/vendor.ts`).

## Why a separate workspace?

The Chromium `build_webui` GNI template has no npm pipeline — source files in
`non_web_component_files` are compiled by Chromium's internal TS compiler, and
`node_modules` is not resolvable. Any JS dependency that needs to run inside a
WebUI page must be pre-bundled and committed into the WebUI resource tree.

Keeping vendored packages in their own workspace (separate from the root
`package.json`) prevents CLI tooling from polluting the bundling toolchain and
makes version pinning explicit.

## Layout

```
vendor/
├── package.json        # pinned versions — source of truth
├── node_modules/       # gitignored; populated by `npm run vendor`
├── entries/            # per-entry source (entry file, templates)
├── manifest.json       # generated: { entry, pkgVersions, sha256 }
└── README.md
```

Declarative configuration: `vendor.config.ts` at the repo root.

## Workflow

```bash
npm run vendor                 # rebuild all entries
npm run vendor -- --entry=pi-agent
npm run vendor -- --check      # CI: verify on-disk artifacts match manifest
npm run vendor -- --list       # print entries + pinned versions
```

Output artifacts land under the path declared in `vendor.config.ts`
(`outDir/outName`). They are committed to git so the Chromium build does not
require running `npm run vendor`.

## Adding a new vendored dependency

1. `cd vendor && npm install <pkg>@<version> --save` to pin the version.
2. Add a `VendorEntry` in `/vendor.config.ts`:
   - `format: "esm"` for ES modules imported by WebUI TS code.
   - `format: "iife-string"` for scripts injected into remote pages (like
     Readability) — requires a `wrapperTemplate`.
   - `format: "customBuild"` for outputs with a non-esbuild build step
     (e.g. Tailwind JIT).
3. If ESM: create an `entries/<name>.entry.ts` that re-exports only the public
   API you actually need (helps tree-shaking). Write a `<outName>.d.ts`
   alongside the artifact, with `declare module 'chrome://.../<outName>'`.
4. If iife-string: write a template at `entries/<name>.tpl.ts` that takes the
   `__BUNDLE__` placeholder.
5. Run `npm run vendor -- --entry=<name>` and commit the artifact + updated
   `manifest.json`.

## Do not edit artifacts by hand

Every generated file has a header identifying its source npm package + version.
Editing the artifact directly will be overwritten on the next vendor run.
