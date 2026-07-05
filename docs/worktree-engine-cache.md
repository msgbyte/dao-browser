# Worktree Engine Cache

This document explains how to create Git worktrees for parallel agent work without
copying a full Chromium `engine/` checkout for every worker.

Dao Browser keeps Chromium in `engine/`, which is very large and gitignored. The
worktree helper creates a local engine cache under `.dao/engine/`, then gives each
Git worktree its own private `engine` symlink backed by a copy-on-write clone of
that cache.

## When To Use This

Use this flow when multiple agents need to work on different branches at the same
time:

```text
dao-browser/                  # primary checkout
dao-browser-feature-a/        # git worktree for agent A
dao-browser-feature-b/        # git worktree for agent B
```

Each worktree gets its own writable Chromium tree, so `npm run import` and
`npm run rebuild` do not stomp on another agent's `engine/src` or
`engine/src/out/dao-debug` state.

## Quick Start

From the primary checkout:

```bash
npm run setup
npm run rebuild
npm run engine:cache:refresh
```

Then create a worker worktree:

```bash
npm run worktree:create -- feature/little-dao \
  --base main \
  --path ../dao-browser-feature-little-dao
```

The new worktree can use normal project commands. If the worktree was created
by an external agent system, run the setup script from inside that worktree
first:

```bash
cd ../dao-browser-feature-little-dao
npm run setup:worktree
npm run rebuild
```

## What The Commands Do

### `npm run engine:cache:refresh`

Refreshes the warm engine cache from the current checkout's `engine/`.

The cache path is:

```text
.dao/engine/cache/warm/<cache-key>/engine
```

The cache key includes:

- Chromium version from `dao.json`
- build flavor, default `dao-debug`
- contents of `dao.json`
- contents of `configs/`
- contents of `src/patches/`
- contents of `src/dao/`
- contents of `branding/`
- contents of `third_party/sparkle/`, when present

Run `npm run rebuild` before refreshing the cache if you want workers to inherit
hot `out/dao-debug` build state.

Use `--force` to replace an existing cache for the current key:

```bash
npm run engine:cache:refresh -- --force
```

### `npm run worktree:create -- <name>`

Creates a Git worktree and attaches a private engine to it.

Example:

```bash
npm run worktree:create -- feature/sidebar-cleanup \
  --base main \
  --path ../dao-browser-feature-sidebar-cleanup
```

This runs:

```bash
git worktree add -b feature/sidebar-cleanup ../dao-browser-feature-sidebar-cleanup main
```

Then it creates:

```text
.dao/engine/worktrees/feature-sidebar-cleanup/engine
```

Finally, it links that private engine into the Git worktree:

```text
../dao-browser-feature-sidebar-cleanup/engine
  -> ../dao-browser/.dao/engine/worktrees/feature-sidebar-cleanup/engine
```

Useful options:

```bash
--branch <branch>       # branch to create, defaults to <name>
--base <ref>            # base ref, defaults to HEAD
--path <path>           # checkout path, defaults to ../dao-browser-<name>
--refresh-cache         # rebuild the warm cache before cloning it
--allow-full-copy       # allow a slow full copy if CoW clone is unavailable
```

The command currently creates a new branch with `git worktree add -b`. If the
branch already exists, Git will fail before the engine is attached.

### `npm run setup:worktree`

Initializes a Git worktree that was created outside the Dao helper.

Run it from inside the new worktree:

```bash
cd ../dao-browser-feature-from-agent
npm run setup:worktree
```

This script runs:

```bash
npm install
npm run worktree:setup
npm run import
```

`npm run worktree:setup` auto-detects the primary checkout with:

```bash
git worktree list --porcelain
```

It then reuses the primary checkout's `.dao/engine` warm cache and attaches a
private engine to the current worktree:

```text
engine -> ../dao-browser/.dao/engine/worktrees/<worktree-id>/engine
```

Use this as the agent initialization command when the agent platform creates the
Git worktree for you:

```bash
npm run setup:worktree
```

If auto-detection fails, run the lower-level setup command with an explicit
primary checkout:

```bash
npm install
npm run worktree:setup -- --primary /Users/moonrailgun/Develop/dao-browser
npm run import
```

### `npm run archive:worktree`

Archives private worktree engine copies. The default behavior depends on where
the command is run:

- from a linked worktree, it deletes that worktree's private engine copy and
  local `engine` symlink
- from the primary checkout, it dry-runs stale `.dao/engine/worktrees/*`
  directories

The command compares:

- active worktrees from `git worktree list --porcelain`
- each `.dao/engine/worktrees/<worktree-id>/manifest.json`
- active worktree `engine` symlink targets

From a linked worktree, run:

```bash
npm run archive:worktree
```

From the primary checkout, the same command is a dry run. After reviewing the
output, pass `--delete` to remove the stale private engine directories:

```bash
npm run archive:worktree -- --delete
```

From a non-primary worktree, pass the primary checkout explicitly if
auto-detection cannot find it:

```bash
npm run archive:worktree -- --primary /Users/moonrailgun/Develop/dao-browser
```

## Directory Layout

The helper keeps all cache state inside the primary checkout:

```text
dao-browser/
  engine/                                  # primary checkout engine
  .dao/
    engine/
      manifest.json
      cache/
        warm/
          <cache-key>/
            engine/                        # warm cache cloned from engine/
      worktrees/
        <worktree-id>/
          manifest.json
          engine/                          # private engine for one git worktree
```

`.dao/` is gitignored.

The primary checkout still uses the normal root `engine/`. The helper does not
move it into `.dao/engine/`.

## Why This Saves Disk

On macOS, the helper uses:

```bash
cp -cR <source-engine> <destination-engine>
```

That asks APFS to create a copy-on-write clone. The clone has independent paths
and can be written safely, but unchanged file data still points at the same
physical disk blocks. Only blocks changed by a worker consume new space.

On Linux, the helper uses:

```bash
cp -a --reflink=always <source-engine> <destination-engine>
```

This requires a filesystem with reflink support, such as btrfs or supported XFS
configurations.

Windows ReFS block clone is not wired yet. On Windows, the helper fails instead
of silently copying the whole `engine/`.

## Cache Refresh Rules

Refresh the cache when any Dao-owned build input changes and you want new
workers to start from a hot engine:

```bash
npm run rebuild
npm run engine:cache:refresh -- --force
```

You can also refresh during worktree creation:

```bash
npm run worktree:create -- feature/new-flow --base main --refresh-cache
```

Existing worktrees keep their own private engines. Refreshing the warm cache does
not mutate engines already attached to worktrees.

## Cleaning Up

Remove the Git worktree first:

```bash
git worktree remove ../dao-browser-feature-sidebar-cleanup
```

Then list stale private engine copies:

```bash
npm run archive:worktree
```

If you are in the primary checkout and the output looks correct, delete them:

```bash
npm run archive:worktree -- --delete
```

If you are inside the linked worktree being retired, `npm run archive:worktree`
deletes that worktree's private engine copy immediately.

Warm caches live under `.dao/engine/cache/warm/`. Remove old cache-key
directories only when no active worker was cloned from them and you no longer
need that warm build state.

## Safety Notes

- Do not share one writable `engine/` between multiple agents.
- Do not symlink every worktree to the same engine cache.
- Do not pass `--allow-full-copy` unless you intentionally want a full disk copy.
- Run Chromium compile confirmation through `npm run rebuild`, not direct
  `autoninja`, `ninja`, `siso`, or `gn gen`.
- Keep deliverable source changes in tracked files under the Git worktree, not
  only inside `engine/`.
