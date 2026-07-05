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

function parseGitWorktreeList(output: string): string[] {
  const worktrees: string[] = [];
  for (const line of output.split(/\r?\n/)) {
    if (line.startsWith("worktree ")) {
      worktrees.push(path.resolve(line.slice("worktree ".length)));
    }
  }
  return worktrees;
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
