import { Command } from "commander";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import {
  existsSync,
  lstatSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  readlinkSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import {
  ENGINE_DIR,
  ROOT_DIR,
  error,
  log,
  success,
  warn,
} from "../utils.js";

const DEFAULT_FLAVOR = "dao-debug";

export type CommandRunner = (
  cmd: string,
  args: string[],
  opts: { cwd: string }
) => void;

export type CommandOutputRunner = (
  cmd: string,
  args: string[],
  opts: { cwd: string }
) => string;

export type EngineCloner = (source: string, dest: string) => void;

export interface EngineStorePaths {
  rootDir: string;
  engineRoot: string;
  cacheDir: string;
  warmCacheDir: string;
  worktreeEnginesDir: string;
  locksDir: string;
  manifestPath: string;
}

export interface CreateDaoWorktreeOptions {
  rootDir: string;
  name: string;
  branch?: string;
  baseRef?: string;
  worktreePath?: string;
  flavor?: string;
  forceRefreshCache?: boolean;
  allowFullCopy?: boolean;
  runner?: CommandRunner;
  cloneEngine?: EngineCloner;
}

export interface CreateDaoWorktreeResult {
  engineId: string;
  cacheKey: string;
  worktreePath: string;
  enginePath: string;
  engineLinkPath: string;
  manifestPath: string;
}

export interface RefreshWarmEngineCacheOptions {
  rootDir: string;
  flavor?: string;
  sourceEnginePath?: string;
  force?: boolean;
  allowFullCopy?: boolean;
  cloneEngine?: EngineCloner;
}

export interface RefreshWarmEngineCacheResult {
  cacheKey: string;
  enginePath: string;
  reused: boolean;
}

export interface SetupDaoWorktreeOptions {
  rootDir: string;
  name?: string;
  primaryRootDir?: string;
  flavor?: string;
  forceRefreshCache?: boolean;
  allowFullCopy?: boolean;
  outputRunner?: CommandOutputRunner;
  cloneEngine?: EngineCloner;
}

export interface SetupDaoWorktreeResult {
  engineId: string;
  cacheKey: string;
  primaryRootDir: string;
  worktreePath: string;
  enginePath: string;
  engineLinkPath: string;
  manifestPath: string;
  reusedEngine: boolean;
}

export interface WorktreeEngineArchiveEntry {
  id: string;
  rootPath: string;
  enginePath: string;
  manifestPath: string;
  worktreePath?: string;
  reason: string;
}

export interface ArchiveStaleDaoWorktreeEnginesOptions {
  rootDir: string;
  deleteStale?: boolean;
  outputRunner?: CommandOutputRunner;
}

export interface ArchiveStaleDaoWorktreeEnginesResult {
  activeWorktreePaths: string[];
  kept: WorktreeEngineArchiveEntry[];
  stale: WorktreeEngineArchiveEntry[];
  deleted: WorktreeEngineArchiveEntry[];
}

export interface ArchiveDaoWorktreeEnginesOptions
    extends ArchiveStaleDaoWorktreeEnginesOptions {
  primaryRootDir?: string;
}

export interface ArchiveDaoWorktreeEnginesResult
    extends ArchiveStaleDaoWorktreeEnginesResult {
  mode: "current-worktree" | "primary-dry-run" | "primary-delete";
  primaryRootDir: string;
  currentRootDir: string;
}

export const worktreeCommand = new Command("worktree")
  .description("Manage git worktrees that reuse Dao's local .dao/engine cache")
  .addCommand(
    new Command("setup")
      .description("Attach a private cached engine to the current git worktree")
      .option("--name <name>", "Engine/worktree id (defaults to current branch)")
      .option(
        "--primary <path>",
        "Primary checkout that owns .dao/engine (auto-detected from git worktree list)"
      )
      .option("--flavor <flavor>", "Warm engine cache flavor", DEFAULT_FLAVOR)
      .option("--refresh-cache", "Refresh the warm cache before cloning")
      .option(
        "--allow-full-copy",
        "Allow slow full-copy fallback when CoW clone is unavailable"
      )
      .action((opts: {
        name?: string;
        primary?: string;
        flavor?: string;
        refreshCache?: boolean;
        allowFullCopy?: boolean;
      }) => {
        try {
          const result = setupDaoWorktree({
            rootDir: ROOT_DIR,
            name: opts.name,
            primaryRootDir: opts.primary ? path.resolve(opts.primary) : undefined,
            flavor: opts.flavor,
            forceRefreshCache: opts.refreshCache,
            allowFullCopy: opts.allowFullCopy,
          });
          success(`Worktree engine ready: ${result.engineLinkPath}`);
          success(`Engine: ${result.enginePath}`);
        } catch (e) {
          error((e as Error).message);
          process.exit(1);
        }
      })
  )
  .addCommand(
    new Command("create")
      .description("Create a git worktree with a private CoW-cloned engine")
      .argument("<name>", "Worktree name or branch")
      .option("--path <path>", "Worktree checkout path")
      .option("--branch <branch>", "Branch to create/use (defaults to name)")
      .option("--base <ref>", "Base ref for git worktree add", "HEAD")
      .option("--flavor <flavor>", "Warm engine cache flavor", DEFAULT_FLAVOR)
      .option("--refresh-cache", "Refresh the warm cache before cloning")
      .option(
        "--allow-full-copy",
        "Allow slow full-copy fallback when CoW clone is unavailable"
      )
      .action((name: string, opts: {
        path?: string;
        branch?: string;
        base?: string;
        flavor?: string;
        refreshCache?: boolean;
        allowFullCopy?: boolean;
      }) => {
        try {
          const result = createDaoWorktree({
            rootDir: ROOT_DIR,
            name,
            branch: opts.branch,
            baseRef: opts.base,
            worktreePath: opts.path ? path.resolve(opts.path) : undefined,
            flavor: opts.flavor,
            forceRefreshCache: opts.refreshCache,
            allowFullCopy: opts.allowFullCopy,
          });
          success(`Created worktree: ${result.worktreePath}`);
          success(`Engine: ${result.engineLinkPath} -> ${result.enginePath}`);
        } catch (e) {
          error((e as Error).message);
          process.exit(1);
        }
      })
  )
  .addCommand(
    new Command("archive")
      .description("Archive .dao/engine worktree copies")
      .option(
        "--primary <path>",
        "Primary checkout that owns .dao/engine (defaults to current checkout when available)"
      )
      .option(
        "--delete",
        "Delete stale worktree engine copies instead of only reporting them"
      )
      .action((opts: {
        primary?: string;
        delete?: boolean;
      }) => {
        try {
          const result = archiveDaoWorktreeEngines({
            rootDir: ROOT_DIR,
            primaryRootDir: opts.primary ? path.resolve(opts.primary) : undefined,
            deleteStale: opts.delete,
          });

          if (result.mode === "current-worktree") {
            const entry = result.deleted[0];
            success(`Deleted current worktree engine: ${entry.rootPath}`);
            return;
          }

          if (result.stale.length === 0) {
            success("No stale worktree engines found.");
            return;
          }

          warn(opts.delete
            ? "Deleted stale worktree engines:"
            : "Stale worktree engines (dry run):");
          for (const entry of result.stale) {
            log(`- ${entry.id}`);
            log(`  engine: ${entry.rootPath}`);
            if (entry.worktreePath) {
              log(`  worktree: ${entry.worktreePath}`);
            }
            log(`  reason: ${entry.reason}`);
          }

          if (opts.delete) {
            success(`Deleted ${result.deleted.length} stale worktree engine(s).`);
          } else {
            warn("Run npm run archive:worktree -- --delete to remove these directories.");
          }
        } catch (e) {
          error((e as Error).message);
          process.exit(1);
        }
      })
  )
  .addCommand(
    new Command("cache")
      .description("Manage .dao/engine warm caches")
      .addCommand(
        new Command("refresh")
          .description("Refresh the warm engine cache from the current engine/")
          .option("--flavor <flavor>", "Warm engine cache flavor", DEFAULT_FLAVOR)
          .option("--source <path>", "Source engine path", ENGINE_DIR)
          .option("--force", "Replace an existing cache for the current key")
          .option(
            "--allow-full-copy",
            "Allow slow full-copy fallback when CoW clone is unavailable"
          )
          .action((opts: {
            flavor?: string;
            source?: string;
            force?: boolean;
            allowFullCopy?: boolean;
          }) => {
            try {
              const result = refreshWarmEngineCache({
                rootDir: ROOT_DIR,
                flavor: opts.flavor,
                sourceEnginePath: path.resolve(opts.source ?? ENGINE_DIR),
                force: opts.force,
                allowFullCopy: opts.allowFullCopy,
              });
              success(
                result.reused
                  ? `Warm cache already exists: ${result.enginePath}`
                  : `Warm cache refreshed: ${result.enginePath}`
              );
            } catch (e) {
              error((e as Error).message);
              process.exit(1);
            }
          })
      )
  );

export function getEngineStorePaths(rootDir: string): EngineStorePaths {
  const engineRoot = path.join(rootDir, ".dao", "engine");
  return {
    rootDir,
    engineRoot,
    cacheDir: path.join(engineRoot, "cache"),
    warmCacheDir: path.join(engineRoot, "cache", "warm"),
    worktreeEnginesDir: path.join(engineRoot, "worktrees"),
    locksDir: path.join(engineRoot, "locks"),
    manifestPath: path.join(engineRoot, "manifest.json"),
  };
}

export function sanitizeWorktreeId(name: string): string {
  const cleaned = name
    .trim()
    .toLowerCase()
    .replace(/[/:\\]+/g, "-")
    .replace(/[^a-z0-9._-]+/g, "-")
    .replace(/-+/g, "-")
    .replace(/^[.-]+/, "")
    .replace(/[.-]+$/, "");
  return cleaned || "worktree";
}

export function computeWarmCacheKey(
  rootDir: string,
  flavor: string = DEFAULT_FLAVOR
): string {
  const daoJsonPath = path.join(rootDir, "dao.json");
  const daoJson = JSON.parse(readFileSync(daoJsonPath, "utf-8"));
  const chromiumVersion = daoJson.version?.version ?? "unknown";
  const hash = createHash("sha256");

  hash.update(`flavor\0${flavor}\0`);
  for (const rel of collectCacheInputFiles(rootDir)) {
    hash.update(`path\0${rel}\0`);
    hash.update(readFileSync(path.join(rootDir, rel)));
    hash.update("\0");
  }

  return `${chromiumVersion}-${flavor}-${hash.digest("hex").slice(0, 16)}`;
}

export function refreshWarmEngineCache(
  opts: RefreshWarmEngineCacheOptions
): RefreshWarmEngineCacheResult {
  const flavor = opts.flavor ?? DEFAULT_FLAVOR;
  const paths = getEngineStorePaths(opts.rootDir);
  const cacheKey = computeWarmCacheKey(opts.rootDir, flavor);
  const cacheRoot = path.join(paths.warmCacheDir, cacheKey);
  const enginePath = path.join(cacheRoot, "engine");
  const sourceEnginePath = opts.sourceEnginePath ?? path.join(opts.rootDir, "engine");

  mkdirSync(paths.warmCacheDir, { recursive: true });
  mkdirSync(paths.locksDir, { recursive: true });

  if (existsSync(enginePath) && !opts.force) {
    writeStoreManifest(paths, { lastWarmCacheKey: cacheKey });
    return { cacheKey, enginePath, reused: true };
  }

  if (!existsSync(sourceEnginePath)) {
    throw new Error(
      `Source engine not found at ${sourceEnginePath}. Run npm run setup first.`
    );
  }

  if (existsSync(cacheRoot)) {
    rmSync(cacheRoot, { recursive: true, force: true });
  }

  const cloner = opts.cloneEngine ??
    ((source, dest) => cloneEngineCopyOnWrite(source, dest, {
      allowFullCopy: opts.allowFullCopy,
    }));
  cloner(sourceEnginePath, enginePath);
  writeStoreManifest(paths, { lastWarmCacheKey: cacheKey });
  return { cacheKey, enginePath, reused: false };
}

export function createDaoWorktree(
  opts: CreateDaoWorktreeOptions
): CreateDaoWorktreeResult {
  const name = opts.name;
  const branch = opts.branch ?? name;
  const baseRef = opts.baseRef ?? "HEAD";
  const flavor = opts.flavor ?? DEFAULT_FLAVOR;
  const engineId = sanitizeWorktreeId(name);
  const paths = getEngineStorePaths(opts.rootDir);
  const worktreePath = opts.worktreePath ??
    path.resolve(opts.rootDir, "..", `dao-browser-${engineId}`);

  const warm = refreshWarmEngineCache({
    rootDir: opts.rootDir,
    flavor,
    force: opts.forceRefreshCache,
    allowFullCopy: opts.allowFullCopy,
    cloneEngine: opts.cloneEngine,
  });

  const engineRoot = path.join(paths.worktreeEnginesDir, engineId);
  const enginePath = path.join(engineRoot, "engine");
  if (existsSync(enginePath)) {
    throw new Error(`Engine for worktree "${engineId}" already exists at ${enginePath}`);
  }

  const runner = opts.runner ?? defaultCommandRunner;
  runner("git", [
    "worktree",
    "add",
    "-b",
    branch,
    worktreePath,
    baseRef,
  ], { cwd: opts.rootDir });

  if (!existsSync(worktreePath)) {
    mkdirSync(worktreePath, { recursive: true });
  }

  const cloner = opts.cloneEngine ??
    ((source, dest) => cloneEngineCopyOnWrite(source, dest, {
      allowFullCopy: opts.allowFullCopy,
    }));
  cloner(warm.enginePath, enginePath);

  const engineLinkPath = attachEngineToWorktree({
    worktreePath,
    enginePath,
  });

  mkdirSync(engineRoot, { recursive: true });
  const manifestPath = path.join(engineRoot, "manifest.json");
  writeFileSync(manifestPath, `${JSON.stringify({
    id: engineId,
    name,
    branch,
    baseRef,
    worktreePath,
    enginePath,
    engineLinkPath,
    cacheKey: warm.cacheKey,
    createdAt: new Date().toISOString(),
  }, null, 2)}\n`);

  return {
    engineId,
    cacheKey: warm.cacheKey,
    worktreePath,
    enginePath,
    engineLinkPath,
    manifestPath,
  };
}

export function setupDaoWorktree(
  opts: SetupDaoWorktreeOptions
): SetupDaoWorktreeResult {
  const outputRunner = opts.outputRunner ?? defaultCommandOutputRunner;
  const primaryRootDir = opts.primaryRootDir ??
    detectPrimaryCheckoutRoot(opts.rootDir, outputRunner);
  const name = opts.name ?? detectCurrentWorktreeName(opts.rootDir, outputRunner);
  const engineId = sanitizeWorktreeId(name);
  const flavor = opts.flavor ?? DEFAULT_FLAVOR;
  const paths = getEngineStorePaths(primaryRootDir);

  const warm = refreshWarmEngineCache({
    rootDir: primaryRootDir,
    flavor,
    force: opts.forceRefreshCache,
    allowFullCopy: opts.allowFullCopy,
    cloneEngine: opts.cloneEngine,
  });

  const engineRoot = path.join(paths.worktreeEnginesDir, engineId);
  const enginePath = path.join(engineRoot, "engine");
  let reusedEngine = false;
  if (existsSync(enginePath)) {
    reusedEngine = true;
  } else {
    const cloner = opts.cloneEngine ??
      ((source, dest) => cloneEngineCopyOnWrite(source, dest, {
        allowFullCopy: opts.allowFullCopy,
      }));
    cloner(warm.enginePath, enginePath);
  }

  const engineLinkPath = attachEngineToWorktree({
    worktreePath: opts.rootDir,
    enginePath,
  });

  mkdirSync(engineRoot, { recursive: true });
  const manifestPath = path.join(engineRoot, "manifest.json");
  writeFileSync(manifestPath, `${JSON.stringify({
    id: engineId,
    name,
    primaryRootDir,
    worktreePath: opts.rootDir,
    enginePath,
    engineLinkPath,
    cacheKey: warm.cacheKey,
    reusedEngine,
    updatedAt: new Date().toISOString(),
  }, null, 2)}\n`);

  return {
    engineId,
    cacheKey: warm.cacheKey,
    primaryRootDir,
    worktreePath: opts.rootDir,
    enginePath,
    engineLinkPath,
    manifestPath,
    reusedEngine,
  };
}

export function archiveDaoWorktreeEngines(
  opts: ArchiveDaoWorktreeEnginesOptions
): ArchiveDaoWorktreeEnginesResult {
  const outputRunner = opts.outputRunner ?? defaultCommandOutputRunner;
  const currentRootDir = path.resolve(opts.rootDir);
  const primaryRootDir = path.resolve(opts.primaryRootDir ??
    detectEngineStoreRoot(currentRootDir, outputRunner));

  if (normalizePath(currentRootDir) !== normalizePath(primaryRootDir)) {
    const current = archiveCurrentDaoWorktreeEngine({
      currentRootDir,
      primaryRootDir,
    });
    return {
      ...current,
      mode: "current-worktree",
      primaryRootDir,
      currentRootDir,
    };
  }

  const primary = archiveStaleDaoWorktreeEngines({
    rootDir: primaryRootDir,
    deleteStale: opts.deleteStale,
    outputRunner,
  });
  return {
    ...primary,
    mode: opts.deleteStale ? "primary-delete" : "primary-dry-run",
    primaryRootDir,
    currentRootDir,
  };
}

export function archiveStaleDaoWorktreeEngines(
  opts: ArchiveStaleDaoWorktreeEnginesOptions
): ArchiveStaleDaoWorktreeEnginesResult {
  const paths = getEngineStorePaths(opts.rootDir);
  const outputRunner = opts.outputRunner ?? defaultCommandOutputRunner;
  const activeWorktreePaths = parseGitWorktreeList(
    outputRunner("git", ["worktree", "list", "--porcelain"], {
      cwd: opts.rootDir,
    })
  );
  const activeWorktreePathSet = new Set(
    activeWorktreePaths.map((p) => normalizePath(p))
  );
  const activeEnginePathSet = collectActiveEngineSymlinkTargets(activeWorktreePaths);
  const kept: WorktreeEngineArchiveEntry[] = [];
  const stale: WorktreeEngineArchiveEntry[] = [];

  if (!existsSync(paths.worktreeEnginesDir)) {
    return { activeWorktreePaths, kept, stale, deleted: [] };
  }

  for (const entry of readdirSync(paths.worktreeEnginesDir, {
    withFileTypes: true,
  })) {
    if (!entry.isDirectory()) continue;

    const rootPath = path.join(paths.worktreeEnginesDir, entry.name);
    const manifestPath = path.join(rootPath, "manifest.json");
    const fallbackEnginePath = path.join(rootPath, "engine");
    const manifest = readWorktreeEngineManifest(manifestPath);
    const id = typeof manifest.id === "string" && manifest.id
      ? manifest.id
      : entry.name;
    const worktreePath = typeof manifest.worktreePath === "string"
      ? path.resolve(manifest.worktreePath)
      : undefined;
    const enginePath = typeof manifest.enginePath === "string"
      ? path.resolve(manifest.enginePath)
      : fallbackEnginePath;
    const archiveEntry: WorktreeEngineArchiveEntry = {
      id,
      rootPath,
      enginePath,
      manifestPath,
      worktreePath,
      reason: "",
    };

    if (worktreePath && activeWorktreePathSet.has(normalizePath(worktreePath))) {
      kept.push({
        ...archiveEntry,
        reason: "worktree is still listed by git",
      });
      continue;
    }

    if (activeEnginePathSet.has(normalizePath(enginePath)) ||
        activeEnginePathSet.has(normalizePath(fallbackEnginePath))) {
      kept.push({
        ...archiveEntry,
        reason: "active worktree engine symlink still points here",
      });
      continue;
    }

    stale.push({
      ...archiveEntry,
      reason: worktreePath
        ? "worktree is no longer listed by git"
        : "manifest is missing a worktreePath",
    });
  }

  const deleted: WorktreeEngineArchiveEntry[] = [];
  if (opts.deleteStale) {
    for (const entry of stale) {
      rmSync(entry.rootPath, { recursive: true, force: true });
      deleted.push(entry);
    }
  }

  return { activeWorktreePaths, kept, stale, deleted };
}

function archiveCurrentDaoWorktreeEngine(opts: {
  currentRootDir: string;
  primaryRootDir: string;
}): ArchiveStaleDaoWorktreeEnginesResult {
  const linkPath = path.join(opts.currentRootDir, "engine");
  let enginePath: string;
  try {
    const stat = lstatSync(linkPath);
    if (!stat.isSymbolicLink()) {
      throw new Error(`${linkPath} is not a symlink`);
    }
    enginePath = path.resolve(opts.currentRootDir, readlinkSync(linkPath));
  } catch (e) {
    throw new Error(
      `Cannot clean current worktree engine: expected engine symlink at ${linkPath}: ${(e as Error).message}`
    );
  }

  const paths = getEngineStorePaths(opts.primaryRootDir);
  if (path.basename(enginePath) !== "engine" ||
      !isPathInside(enginePath, paths.worktreeEnginesDir)) {
    throw new Error(
      `Cannot clean current worktree engine: ${linkPath} points outside ${paths.worktreeEnginesDir}`
    );
  }

  const rootPath = path.dirname(enginePath);
  const manifestPath = path.join(rootPath, "manifest.json");
  const manifest = readWorktreeEngineManifest(manifestPath);
  const id = typeof manifest.id === "string" && manifest.id
    ? manifest.id
    : path.basename(rootPath);
  const worktreePath = typeof manifest.worktreePath === "string"
    ? path.resolve(manifest.worktreePath)
    : opts.currentRootDir;
  const entry: WorktreeEngineArchiveEntry = {
    id,
    rootPath,
    enginePath,
    manifestPath,
    worktreePath,
    reason: "current linked worktree requested cleanup",
  };

  rmSync(linkPath, { force: true });
  rmSync(rootPath, { recursive: true, force: true });

  return {
    activeWorktreePaths: [],
    kept: [],
    stale: [entry],
    deleted: [entry],
  };
}

export function attachEngineToWorktree(opts: {
  worktreePath: string;
  enginePath: string;
}): string {
  const linkPath = path.join(opts.worktreePath, "engine");
  mkdirSync(opts.worktreePath, { recursive: true });

  if (existsSync(linkPath)) {
    const stat = lstatSync(linkPath);
    if (!stat.isSymbolicLink()) {
      throw new Error(`Cannot attach engine: ${linkPath} already exists`);
    }
    const currentTarget = readlinkSync(linkPath);
    const resolved = path.resolve(opts.worktreePath, currentTarget);
    if (resolved === opts.enginePath) {
      return linkPath;
    }
    throw new Error(
      `Cannot attach engine: ${linkPath} already points to ${currentTarget}`
    );
  }

  const relativeTarget = path.relative(opts.worktreePath, opts.enginePath);
  symlinkSync(relativeTarget, linkPath, "dir");
  return linkPath;
}

export function cloneEngineCopyOnWrite(
  source: string,
  dest: string,
  opts?: { allowFullCopy?: boolean }
): void {
  if (!existsSync(source)) {
    throw new Error(`Engine clone source not found: ${source}`);
  }
  if (existsSync(dest)) {
    throw new Error(`Engine clone destination already exists: ${dest}`);
  }

  mkdirSync(path.dirname(dest), { recursive: true });
  try {
    if (process.platform === "darwin") {
      execFileSync("cp", ["-cR", source, dest], { stdio: "inherit" });
      return;
    }
    if (process.platform === "linux") {
      execFileSync("cp", ["-a", "--reflink=always", source, dest], {
        stdio: "inherit",
      });
      return;
    }
    if (process.platform === "win32") {
      throw new Error(
        "Windows ReFS block clone is not wired yet; run this on APFS or pass --allow-full-copy explicitly."
      );
    }
  } catch (e) {
    if (!opts?.allowFullCopy) {
      throw new Error(
        `Failed to CoW clone engine from ${source} to ${dest}: ${(e as Error).message}`
      );
    }
    warn("CoW clone failed; falling back to a full engine copy");
  }

  if (!opts?.allowFullCopy) {
    throw new Error(`Unsupported platform for CoW engine clone: ${process.platform}`);
  }
  execFileSync("cp", ["-R", source, dest], { stdio: "inherit" });
}

function detectPrimaryCheckoutRoot(
  currentRoot: string,
  outputRunner: CommandOutputRunner
): string {
  const output = outputRunner("git", ["worktree", "list", "--porcelain"], {
    cwd: currentRoot,
  });
  const worktrees = parseGitWorktreeList(output);
  const current = path.resolve(currentRoot);
  const otherWorktrees = worktrees.filter((p) => path.resolve(p) !== current);
  const withDaoCache = otherWorktrees.find((p) =>
    existsSync(path.join(p, ".dao", "engine")) || existsSync(path.join(p, "engine"))
  );
  if (withDaoCache) {
    return withDaoCache;
  }
  return otherWorktrees[0] ?? worktrees[0] ?? currentRoot;
}

function detectCurrentWorktreeName(
  currentRoot: string,
  outputRunner: CommandOutputRunner
): string {
  try {
    const branch = outputRunner("git", ["rev-parse", "--abbrev-ref", "HEAD"], {
      cwd: currentRoot,
    }).trim();
    if (branch && branch !== "HEAD") {
      return branch;
    }
  } catch {
    // Detached HEAD or non-git test fixture; use the directory name below.
  }
  return path.basename(currentRoot);
}

function detectEngineStoreRoot(
  currentRoot: string,
  outputRunner: CommandOutputRunner
): string {
  if (existsSync(path.join(currentRoot, ".dao", "engine"))) {
    return currentRoot;
  }
  return detectPrimaryCheckoutRoot(currentRoot, outputRunner);
}

function parseGitWorktreeList(output: string): string[] {
  const worktrees: string[] = [];
  for (const line of output.split(/\r?\n/)) {
    if (line.startsWith("worktree ")) {
      worktrees.push(path.resolve(line.slice("worktree ".length)));
    }
  }
  return worktrees;
}

function readWorktreeEngineManifest(manifestPath: string): Record<string, unknown> {
  if (!existsSync(manifestPath)) {
    return {};
  }
  try {
    return JSON.parse(readFileSync(manifestPath, "utf-8")) as Record<string, unknown>;
  } catch {
    return {};
  }
}

function collectActiveEngineSymlinkTargets(worktreePaths: string[]): Set<string> {
  const targets = new Set<string>();
  for (const worktreePath of worktreePaths) {
    const linkPath = path.join(worktreePath, "engine");
    try {
      const stat = lstatSync(linkPath);
      if (!stat.isSymbolicLink()) continue;
      targets.add(normalizePath(path.resolve(worktreePath, readlinkSync(linkPath))));
    } catch {
      // Missing worktree paths are ignored; git's worktree list is authoritative.
    }
  }
  return targets;
}

function normalizePath(value: string): string {
  return path.resolve(value);
}

function isPathInside(child: string, parent: string): boolean {
  const relative = path.relative(path.resolve(parent), path.resolve(child));
  return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
}

function collectCacheInputFiles(rootDir: string): string[] {
  const inputs = [
    "dao.json",
    "configs",
    "src/patches",
    "src/dao",
    "branding",
    "third_party/sparkle",
  ];
  const files: string[] = [];
  for (const input of inputs) {
    const fullPath = path.join(rootDir, input);
    if (!existsSync(fullPath)) continue;
    const stat = lstatSync(fullPath);
    if (stat.isDirectory()) {
      collectFiles(fullPath, rootDir, files);
    } else if (stat.isFile()) {
      files.push(input);
    }
  }
  return files.sort();
}

function collectFiles(dir: string, rootDir: string, out: string[]): void {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      collectFiles(fullPath, rootDir, out);
    } else if (entry.isFile()) {
      out.push(path.relative(rootDir, fullPath));
    }
  }
}

function writeStoreManifest(
  paths: EngineStorePaths,
  patch: Record<string, unknown>
): void {
  mkdirSync(paths.engineRoot, { recursive: true });
  const current = existsSync(paths.manifestPath)
    ? JSON.parse(readFileSync(paths.manifestPath, "utf-8"))
    : {};
  writeFileSync(paths.manifestPath, `${JSON.stringify({
    ...current,
    ...patch,
    updatedAt: new Date().toISOString(),
  }, null, 2)}\n`);
}

const defaultCommandRunner: CommandRunner = (cmd, args, opts) => {
  log(`${cmd} ${args.join(" ")}`);
  execFileSync(cmd, args, {
    cwd: opts.cwd,
    stdio: "inherit",
  });
};

const defaultCommandOutputRunner: CommandOutputRunner = (cmd, args, opts) => {
  return execFileSync(cmd, args, {
    cwd: opts.cwd,
    encoding: "utf-8",
    stdio: ["ignore", "pipe", "pipe"],
  }).trim();
};
