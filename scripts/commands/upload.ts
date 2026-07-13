import { Command } from "commander";
import { existsSync, statSync, readdirSync } from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import readline from "node:readline";
import chalk from "chalk";
import {
  ROOT_DIR,
  log,
  success,
  warn,
  error,
  which,
} from "../utils.js";

interface UploadOptions {
  bucket?: string;
  prefix?: string;
  key?: string;
  contentType?: string;
  remote?: boolean;
  dryRun?: boolean;
}

interface CleanupOptions {
  bucket?: string;
  prefix?: string;
  keep?: string;
  remote?: boolean;
  dryRun?: boolean;
}

export interface R2Object {
  key: string;
  size?: number;
  uploaded?: string;
}

export interface CleanupCandidate extends R2Object {
  version: string;
}

// Required env vars for wrangler to talk to R2 non-interactively. Wrangler
// reads CLOUDFLARE_API_TOKEN + CLOUDFLARE_ACCOUNT_ID directly from the env
// when --remote is used; we surface a clear error if either is missing.
const REQUIRED_ENV = ["CLOUDFLARE_ACCOUNT_ID", "CLOUDFLARE_API_TOKEN"] as const;

export const uploadCommand = new Command("upload");

uploadCommand
  .description(
    "Upload build artifacts to Cloudflare R2 via wrangler. " +
      "Files are taken from positional args or stdin (one path per line)."
  )
  .argument("[files...]", "Files to upload (or pipe paths via stdin)")
  .option("-b, --bucket <name>", "R2 bucket name (or $R2_BUCKET)")
  .option(
    "-p, --prefix <prefix>",
    "Key prefix prepended to each uploaded file's basename",
    ""
  )
  .option(
    "-k, --key <key>",
    "Explicit object key (only valid when uploading a single file)"
  )
  .option(
    "--content-type <type>",
    "Override Content-Type (auto-detected by extension when omitted)"
  )
  .option(
    "--no-remote",
    "Upload to local wrangler simulator instead of the real bucket"
  )
  .option("--dry-run", "Print what would be uploaded without calling wrangler")
  .action(async (files: string[], opts: UploadOptions) => {
    const bucket = opts.bucket || process.env.R2_BUCKET;
    if (!bucket) {
      error(
        "Bucket not specified. Pass --bucket <name> or set R2_BUCKET in the environment."
      );
      process.exit(1);
    }

    if (!opts.dryRun && opts.remote !== false) {
      const missing = REQUIRED_ENV.filter((k) => !process.env[k]);
      if (missing.length > 0) {
        error(
          `Missing required env var(s): ${missing.join(", ")}.\n` +
            "Set these before uploading to a real R2 bucket:\n" +
            "  export CLOUDFLARE_ACCOUNT_ID=<account-id>\n" +
            "  export CLOUDFLARE_API_TOKEN=<token-with-r2-edit>\n" +
            "Or pass --no-remote to target the local wrangler simulator."
        );
        process.exit(1);
      }
    }

    const wrangler = which("wrangler") || which("npx");
    if (!wrangler) {
      error(
        "wrangler CLI not found. Install it with: npm i -g wrangler\n" +
          "Or run via npx (requires npx in PATH)."
      );
      process.exit(1);
    }

    const inputs = await collectInputs(files);
    if (inputs.length === 0) {
      error(
        "No files to upload. Pass paths as arguments or pipe them via stdin."
      );
      process.exit(1);
    }

    if (opts.key && inputs.length !== 1) {
      error("--key can only be used when uploading exactly one file.");
      process.exit(1);
    }

    const targets = expandTargets(inputs);
    log(
      `Uploading ${targets.length} file(s) to R2 bucket ${chalk.cyan(bucket)}` +
        (opts.remote === false ? chalk.dim(" (local simulator)") : "")
    );

    let failures = 0;
    for (const t of targets) {
      const key = computeKey(t, opts);
      const contentType =
        opts.contentType || guessContentType(t.absPath) || undefined;
      if (opts.dryRun) {
        console.log(
          chalk.dim(
            `[dry-run] ${t.absPath} -> r2://${bucket}/${key}` +
              (contentType ? ` (${contentType})` : "")
          )
        );
        continue;
      }
      const ok = await uploadOne({
        bucket,
        key,
        filePath: t.absPath,
        contentType,
        remote: opts.remote !== false,
      });
      if (!ok) failures += 1;
    }

    if (failures > 0) {
      error(`${failures} file(s) failed to upload.`);
      process.exit(1);
    }
    success(opts.dryRun ? "Dry-run complete" : "All files uploaded");
  });

uploadCommand
  .command("cleanup")
  .description(
    "Interactively delete old Dao release artifacts from Cloudflare R2."
  )
  .option("-b, --bucket <name>", "R2 bucket name (or $R2_BUCKET)")
  .option(
    "-p, --prefix <prefix>",
    "Only scan objects with this key prefix",
    ""
  )
  .option(
    "--keep <count>",
    "Number of latest Dao versions to protect from cleanup",
    "2"
  )
  .option(
    "--no-remote",
    "Target local wrangler simulator instead of the real bucket"
  )
  .option("--dry-run", "Print selected deletions without calling wrangler")
  .action(async (opts: CleanupOptions) => {
    const bucket = opts.bucket || process.env.R2_BUCKET;
    if (!bucket) {
      error(
        "Bucket not specified. Pass --bucket <name> or set R2_BUCKET in the environment."
      );
      process.exit(1);
    }

    if (!opts.dryRun && opts.remote !== false) {
      const missing = REQUIRED_ENV.filter((k) => !process.env[k]);
      if (missing.length > 0) {
        error(
          `Missing required env var(s): ${missing.join(", ")}.\n` +
            "Set these before deleting from a real R2 bucket:\n" +
            "  export CLOUDFLARE_ACCOUNT_ID=<account-id>\n" +
            "  export CLOUDFLARE_API_TOKEN=<token-with-r2-edit>\n" +
            "Or pass --no-remote to target the local wrangler simulator."
        );
        process.exit(1);
      }
    }

    if (!which("wrangler")) {
      error("wrangler CLI not found. Install it with: npm i -g wrangler");
      process.exit(1);
    }

    const keep = parseKeepCount(opts.keep);
    log(
      `Listing R2 bucket ${chalk.cyan(bucket)}` +
        (opts.prefix ? ` with prefix ${chalk.cyan(opts.prefix)}` : "") +
        (opts.remote === false ? chalk.dim(" (local simulator)") : "")
    );
    const objects = await listR2Objects({
      bucket,
      prefix: opts.prefix || "",
      remote: opts.remote !== false,
    });
    const candidates = getCleanupCandidates(objects, keep);
    if (candidates.length === 0) {
      success(`No old Dao release artifacts found (keeping latest ${keep}).`);
      return;
    }

    const selected = await selectCleanupCandidates(candidates, keep);
    if (selected.length === 0) {
      warn("No objects selected. Nothing deleted.");
      return;
    }

    printDeletionSummary(selected);
    const confirmed = await confirmDeletion(selected);
    if (!confirmed) {
      warn("Cleanup cancelled. Nothing deleted.");
      return;
    }

    if (opts.dryRun) {
      for (const obj of selected) {
        console.log(chalk.dim(`[dry-run] delete r2://${bucket}/${obj.key}`));
      }
      success("Dry-run complete");
      return;
    }

    let failures = 0;
    for (const obj of selected) {
      const ok = await deleteR2Object({
        bucket,
        key: obj.key,
        remote: opts.remote !== false,
      });
      if (!ok) failures += 1;
    }

    if (failures > 0) {
      error(`${failures} object(s) failed to delete.`);
      process.exit(1);
    }
    success(`Deleted ${selected.length} object(s) from R2`);
  });

interface FileTarget {
  absPath: string;
  // Path used to derive the key when no --key is set: file's basename, or
  // for directories the path relative to the directory root.
  relPath: string;
}

async function collectInputs(args: string[]): Promise<string[]> {
  if (args.length > 0) return args;
  if (process.stdin.isTTY) return [];
  const data = await readStdin();
  return data
    .split(/\r?\n/)
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
}

function readStdin(): Promise<string> {
  return new Promise((resolve, reject) => {
    let buf = "";
    process.stdin.setEncoding("utf-8");
    process.stdin.on("data", (chunk) => (buf += chunk));
    process.stdin.on("end", () => resolve(buf));
    process.stdin.on("error", reject);
  });
}

// Resolve each input to one or more concrete files. Directories expand
// recursively so callers can do `dao upload dist/` to ship the whole folder.
function expandTargets(inputs: string[]): FileTarget[] {
  const out: FileTarget[] = [];
  for (const raw of inputs) {
    const abs = path.isAbsolute(raw) ? raw : path.resolve(ROOT_DIR, raw);
    if (!existsSync(abs)) {
      warn(`Skipping missing path: ${raw}`);
      continue;
    }
    const st = statSync(abs);
    if (st.isFile()) {
      out.push({ absPath: abs, relPath: path.basename(abs) });
    } else if (st.isDirectory()) {
      walkDir(abs, abs, out);
    } else {
      warn(`Skipping non-regular path: ${raw}`);
    }
  }
  return out;
}

function walkDir(root: string, dir: string, out: FileTarget[]): void {
  for (const name of readdirSync(dir)) {
    const p = path.join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) {
      walkDir(root, p, out);
    } else if (st.isFile()) {
      out.push({ absPath: p, relPath: path.relative(root, p) });
    }
  }
}

function computeKey(t: FileTarget, opts: UploadOptions): string {
  if (opts.key) return opts.key;
  const prefix = (opts.prefix || "").replace(/^\/+|\/+$/g, "");
  // Normalize Windows-style separators just in case.
  const rel = t.relPath.split(path.sep).join("/");
  return prefix ? `${prefix}/${rel}` : rel;
}

interface UploadArgs {
  bucket: string;
  key: string;
  filePath: string;
  contentType?: string;
  remote: boolean;
}

function uploadOne(args: UploadArgs): Promise<boolean> {
  const cliArgs = [
    "r2",
    "object",
    "put",
    `${args.bucket}/${args.key}`,
    "--file",
    args.filePath,
  ];
  if (args.contentType) {
    cliArgs.push("--content-type", args.contentType);
  }
  if (args.remote) {
    cliArgs.push("--remote");
  }

  console.log(
    chalk.dim(`$ wrangler ${cliArgs.map(shellEscape).join(" ")}`)
  );

  return new Promise((resolve) => {
    const child = spawn("wrangler", cliArgs, {
      cwd: ROOT_DIR,
      stdio: "inherit",
      env: process.env,
    });
    child.on("error", (err) => {
      error(`Failed to launch wrangler: ${err.message}`);
      resolve(false);
    });
    child.on("close", (code) => {
      if (code === 0) {
        success(`Uploaded ${args.key}`);
        resolve(true);
      } else {
        error(`wrangler exited with code ${code} for ${args.key}`);
        resolve(false);
      }
    });
  });
}

interface ListR2ObjectsArgs {
  bucket: string;
  prefix: string;
  remote: boolean;
}

// Wrangler 4.x dropped `r2 object list`, so listing goes directly through the
// Cloudflare REST API. `put` and `delete` still exist in wrangler and are kept
// on the wrangler path so we don't have to sign S3 SigV4 requests here.
async function listR2Objects(args: ListR2ObjectsArgs): Promise<R2Object[]> {
  if (!args.remote) {
    error(
      "Listing the local wrangler simulator is not supported.\n" +
        "Wrangler 4.x removed `r2 object list`; cleanup now uses the\n" +
        "Cloudflare REST API, which only targets real R2 buckets.\n" +
        "Re-run without --no-remote to list the live bucket."
    );
    process.exit(1);
  }

  const accountId = process.env.CLOUDFLARE_ACCOUNT_ID;
  const apiToken = process.env.CLOUDFLARE_API_TOKEN;
  if (!accountId || !apiToken) {
    error(
      "Listing R2 objects requires CLOUDFLARE_ACCOUNT_ID and CLOUDFLARE_API_TOKEN."
    );
    process.exit(1);
  }

  const baseUrl =
    `https://api.cloudflare.com/client/v4/accounts/${accountId}` +
    `/r2/buckets/${encodeURIComponent(args.bucket)}/objects`;

  console.log(
    chalk.dim(
      `$ GET /accounts/${accountId}/r2/buckets/${args.bucket}/objects` +
        (args.prefix ? `?prefix=${args.prefix}` : "")
    )
  );

  const all: R2Object[] = [];
  let cursor: string | undefined;
  for (let page = 0; page < 100; page += 1) {
    const url = new URL(baseUrl);
    url.searchParams.set("per_page", "1000");
    if (args.prefix) url.searchParams.set("prefix", args.prefix);
    if (cursor) url.searchParams.set("cursor", cursor);

    let res: Response;
    try {
      res = await fetch(url, {
        headers: {
          Authorization: `Bearer ${apiToken}`,
          Accept: "application/json",
        },
      });
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      error(`Failed to reach Cloudflare API: ${message}`);
      process.exit(1);
    }

    if (!res.ok) {
      const body = await res.text().catch(() => "");
      error(
        `Cloudflare API returned ${res.status} ${res.statusText} while listing R2 objects.` +
          (body ? `\n${body}` : "")
      );
      process.exit(1);
    }

    let json: unknown;
    try {
      json = await res.json();
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      error(`Failed to parse Cloudflare API response: ${message}`);
      process.exit(1);
    }

    if (!isRecord(json) || json.success !== true) {
      const detail =
        isRecord(json) && Array.isArray(json.errors) && json.errors.length > 0
          ? JSON.stringify(json.errors)
          : "<no error detail>";
      error(`Cloudflare API reported failure listing R2 objects: ${detail}`);
      process.exit(1);
    }

    try {
      all.push(...parseR2ListOutput(JSON.stringify(json)));
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      error(`Failed to parse Cloudflare list response: ${message}`);
      process.exit(1);
    }

    const info = isRecord(json.result_info) ? json.result_info : null;
    const nextCursor =
      info && typeof info.cursor === "string" ? info.cursor : "";
    const truncated = !!info && info.is_truncated === true;
    if (!truncated || !nextCursor) {
      return all;
    }
    cursor = nextCursor;
  }

  error("Aborting R2 list after 100 pages — bucket may be unexpectedly large.");
  process.exit(1);
}

interface DeleteR2ObjectArgs {
  bucket: string;
  key: string;
  remote: boolean;
}

function deleteR2Object(args: DeleteR2ObjectArgs): Promise<boolean> {
  const cliArgs = [
    "r2",
    "object",
    "delete",
    `${args.bucket}/${args.key}`,
    "--force",
  ];
  if (args.remote) {
    cliArgs.push("--remote");
  }

  console.log(chalk.dim(`$ wrangler ${cliArgs.map(shellEscape).join(" ")}`));

  return new Promise((resolve) => {
    const child = spawn("wrangler", cliArgs, {
      cwd: ROOT_DIR,
      stdio: "inherit",
      env: process.env,
    });
    child.on("error", (err) => {
      error(`Failed to launch wrangler: ${err.message}`);
      resolve(false);
    });
    child.on("close", (code) => {
      if (code === 0) {
        success(`Deleted ${args.key}`);
        resolve(true);
      } else {
        error(`wrangler exited with code ${code} for ${args.key}`);
        resolve(false);
      }
    });
  });
}

export function parseR2ListOutput(output: string): R2Object[] {
  const parsed: unknown = JSON.parse(output);
  const rows = Array.isArray(parsed)
    ? parsed
    : isRecord(parsed) && Array.isArray(parsed.objects)
      ? parsed.objects
      : isRecord(parsed) && Array.isArray(parsed.result)
        ? parsed.result
        : null;
  if (!rows) {
    throw new Error("expected a JSON array or an object with objects/result");
  }
  return rows.flatMap((row) => {
    if (!isRecord(row) || typeof row.key !== "string") {
      return [];
    }
    // Wrangler's `r2 object list` returned `uploaded`; Cloudflare's REST API
    // returns `last_modified`. Accept either so the parser works for both.
    const uploaded =
      typeof row.uploaded === "string"
        ? row.uploaded
        : typeof row.last_modified === "string"
          ? row.last_modified
          : undefined;
    return [{
      key: row.key,
      size: typeof row.size === "number" ? row.size : undefined,
      uploaded,
    }];
  });
}

export function getCleanupCandidates(
  objects: R2Object[],
  keepVersions: number
): CleanupCandidate[] {
  const releaseObjects = objects.flatMap((obj) => {
    const version = parseDaoReleaseVersion(obj.key);
    return version ? [{ ...obj, version }] : [];
  });

  // DMG and delta files use different version number schemes (display version
  // vs CFBundleVersion), so compute the protected set independently for each
  // kind. Mixing them into one pool causes incorrect keep decisions.
  const dmgs = releaseObjects.filter((o) => o.key.endsWith(".dmg"));
  const deltas = releaseObjects.filter((o) => o.key.endsWith(".delta"));

  function protectedSet(items: CleanupCandidate[]): Set<string> {
    const sorted = Array.from(new Set(items.map((o) => o.version))).sort(
      compareVersionsDesc
    );
    return new Set(sorted.slice(0, keepVersions));
  }

  const protectedDmg = protectedSet(dmgs);
  const protectedDelta = protectedSet(deltas);

  const candidates = releaseObjects.filter((obj) => {
    if (obj.key.endsWith(".dmg")) return !protectedDmg.has(obj.version);
    if (obj.key.endsWith(".delta")) return !protectedDelta.has(obj.version);
    return true;
  });

  return candidates.sort((a, b) => {
    const byKind = releaseKindRank(a.key) - releaseKindRank(b.key);
    if (byKind !== 0) return byKind;
    const byVersion = compareVersionsDesc(a.version, b.version);
    return byVersion === 0 ? a.key.localeCompare(b.key) : byVersion;
  });
}

function parseDaoReleaseVersion(key: string): string | null {
  const base = path.posix.basename(key);
  const dmg = base.match(/^dao-browser-(\d+\.\d+\.\d+)-mac-[\w-]+\.dmg$/);
  if (dmg) return dmg[1];

  // Sparkle names delta files using CFBundleVersion (build number), e.g.
  // "Dao17.0-13.0.delta" where 17.0 is the target build and 13.0 the source.
  const delta = base.match(/^Dao(\d+(?:\.\d+)*)-\d+(?:\.\d+)*\.delta$/);
  if (delta) return delta[1];

  return null;
}

function compareVersionsDesc(a: string, b: string): number {
  const pa = a.split(".").map((part) => Number.parseInt(part, 10));
  const pb = b.split(".").map((part) => Number.parseInt(part, 10));
  for (let i = 0; i < Math.max(pa.length, pb.length); i += 1) {
    const av = pa[i] || 0;
    const bv = pb[i] || 0;
    if (av !== bv) return bv - av;
  }
  return 0;
}

function releaseKindRank(key: string): number {
  const base = path.posix.basename(key);
  if (base.endsWith(".dmg")) return 0;
  if (base.endsWith(".delta")) return 1;
  return 2;
}

function isDmgCandidate(candidate: CleanupCandidate): boolean {
  return candidate.key.endsWith(".dmg");
}

function isDeltaCandidate(candidate: CleanupCandidate): boolean {
  return candidate.key.endsWith(".delta");
}

export function getAssociatedDeltaIndexes(
  candidates: CleanupCandidate[]
): Map<number, number[]> {
  const dmgVersions = Array.from(
    new Set(candidates.filter(isDmgCandidate).map((item) => item.version))
  ).sort(compareVersionsDesc);
  const deltaVersions = Array.from(
    new Set(candidates.filter(isDeltaCandidate).map((item) => item.version))
  ).sort(compareVersionsDesc);
  const deltaVersionByDmgVersion = new Map<string, string>();
  for (
    let i = 0;
    i < Math.min(dmgVersions.length, deltaVersions.length);
    i += 1
  ) {
    deltaVersionByDmgVersion.set(dmgVersions[i], deltaVersions[i]);
  }

  const indexByDeltaVersion = new Map<string, number[]>();
  candidates.forEach((item, index) => {
    if (!isDeltaCandidate(item)) return;
    const indexes = indexByDeltaVersion.get(item.version) || [];
    indexes.push(index);
    indexByDeltaVersion.set(item.version, indexes);
  });

  const associations = new Map<number, number[]>();
  candidates.forEach((item, index) => {
    if (!isDmgCandidate(item)) return;
    const deltaVersion = deltaVersionByDmgVersion.get(item.version);
    if (!deltaVersion) return;
    const deltaIndexes = indexByDeltaVersion.get(deltaVersion);
    if (deltaIndexes && deltaIndexes.length > 0) {
      associations.set(index, deltaIndexes);
    }
  });
  return associations;
}

function addAssociatedDeltas(
  selected: Set<number>,
  associations: Map<number, number[]>
): void {
  for (const [dmgIndex, deltaIndexes] of associations) {
    if (!selected.has(dmgIndex)) continue;
    for (const deltaIndex of deltaIndexes) selected.add(deltaIndex);
  }
}

export function getSelectableCleanupIndexes(
  candidates: CleanupCandidate[]
): number[] {
  const associatedDeltaIndexes = getAssociatedDeltaIndexes(candidates);
  const autoDeltaIndexes = new Set(
    Array.from(associatedDeltaIndexes.values()).flat()
  );
  return candidates.flatMap((_item, index) =>
    autoDeltaIndexes.has(index) ? [] : [index]
  );
}

function parseKeepCount(value: string | undefined): number {
  const n = Number.parseInt(value || "2", 10);
  if (!Number.isInteger(n) || n < 0) {
    error("--keep must be a non-negative integer.");
    process.exit(1);
  }
  return n;
}

function formatBytes(size: number | undefined): string {
  if (size === undefined) return "-";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = size;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

export interface CleanupSpaceEstimate {
  knownBytes: number;
  unknownSizeCount: number;
}

export function getCleanupSpaceEstimate(
  objects: R2Object[]
): CleanupSpaceEstimate {
  return objects.reduce<CleanupSpaceEstimate>(
    (estimate, object) => {
      if (object.size === undefined) {
        estimate.unknownSizeCount += 1;
      } else {
        estimate.knownBytes += object.size;
      }
      return estimate;
    },
    { knownBytes: 0, unknownSizeCount: 0 }
  );
}

export function formatCleanupSpaceEstimate(objects: R2Object[]): string {
  const { knownBytes, unknownSizeCount } = getCleanupSpaceEstimate(objects);
  if (unknownSizeCount === 0) {
    return `Estimated space to free: ${formatBytes(knownBytes)}`;
  }
  const objectLabel = unknownSizeCount === 1 ? "object size" : "object sizes";
  return (
    `Estimated space to free: at least ${formatBytes(knownBytes)} ` +
    `(${unknownSizeCount} ${objectLabel} unknown)`
  );
}

export function formatCleanupSelectionStatus(
  selected: CleanupCandidate[],
  candidateCount: number
): string {
  return (
    `Selected ${selected.length}/${candidateCount} objects · ` +
    formatCleanupSpaceEstimate(selected)
  );
}

export function formatCleanupConfirmation(
  selected: CleanupCandidate[]
): string {
  return (
    `Delete ${selected.length} object(s) permanently? ` +
    `${formatCleanupSpaceEstimate(selected)} [Y/n] `
  );
}

async function selectCleanupCandidates(
  candidates: CleanupCandidate[],
  keep: number
): Promise<CleanupCandidate[]> {
  if (!process.stdin.isTTY || !process.stdout.isTTY) {
    warn("Interactive terminal required. Run cleanup from a TTY to select objects.");
    return [];
  }

  readline.emitKeypressEvents(process.stdin);
  const wasRaw = process.stdin.isRaw;
  process.stdin.setRawMode(true);

  let cursor = 0;
  let scrollOffset = 0;
  const selected = new Set<number>();
  const associatedDeltaIndexes = getAssociatedDeltaIndexes(candidates);
  const selectableIndexes = getSelectableCleanupIndexes(candidates);
  const terminalRows = () => process.stdout.rows || 24;
  const visibleRows = () => Math.max(5, terminalRows() - 7);
  const updateScrollOffset = () => {
    const rows = visibleRows();
    if (cursor < scrollOffset) {
      scrollOffset = cursor;
    } else if (cursor >= scrollOffset + rows) {
      scrollOffset = cursor - rows + 1;
    }
  };
  const toggleSelection = (index: number) => {
    const candidateIndex = selectableIndexes[index];
    if (selected.has(candidateIndex)) {
      selected.delete(candidateIndex);
      const deltaIndexes = associatedDeltaIndexes.get(candidateIndex) || [];
      for (const deltaIndex of deltaIndexes) selected.delete(deltaIndex);
    } else {
      selected.add(candidateIndex);
      const deltaIndexes = associatedDeltaIndexes.get(candidateIndex) || [];
      for (const deltaIndex of deltaIndexes) selected.add(deltaIndex);
    }
    addAssociatedDeltas(selected, associatedDeltaIndexes);
  };
  const render = () => {
    addAssociatedDeltas(selected, associatedDeltaIndexes);
    updateScrollOffset();
    process.stdout.write("\x1B[2J\x1B[H");
    console.log(chalk.blue("dao") + " R2 cleanup");
    console.log(
      chalk.dim(
        `Old Dao release artifacts found. Latest ${keep} version(s) are protected.`
      )
    );
    console.log(
      chalk.dim(
        "Up/Down move  Space select  a all  i invert  Enter confirm  q cancel\n"
      )
    );
    const end = Math.min(scrollOffset + visibleRows(), selectableIndexes.length);
    selectableIndexes
      .slice(scrollOffset, end)
      .forEach((candidateIndex, relativeIndex) => {
        const index = scrollOffset + relativeIndex;
        const item = candidates[candidateIndex];
        const pointer = index === cursor ? chalk.cyan("›") : " ";
        const mark = selected.has(candidateIndex) ? chalk.green("●") : "○";
        console.log(
          `${pointer} ${mark} ${item.version.padEnd(8)} ` +
            `${formatBytes(item.size).padStart(9)}  ${item.key}`
        );
      });
    console.log(
      chalk.dim(
        `\nShowing ${scrollOffset + 1}-${end}/${selectableIndexes.length}. ` +
          formatCleanupSelectionStatus(
            Array.from(selected).map((index) => candidates[index]),
            candidates.length
          )
      )
    );
  };

  render();
  const result = await new Promise<CleanupCandidate[]>((resolve) => {
    const onKeypress = (_str: string, key: readline.Key) => {
      if (key.name === "down" || key.name === "j") {
        cursor = Math.min(cursor + 1, selectableIndexes.length - 1);
        render();
      } else if (key.name === "up" || key.name === "k") {
        cursor = Math.max(cursor - 1, 0);
        render();
      } else if (key.name === "pagedown") {
        cursor = Math.min(cursor + visibleRows(), selectableIndexes.length - 1);
        render();
      } else if (key.name === "pageup") {
        cursor = Math.max(cursor - visibleRows(), 0);
        render();
      } else if (key.name === "home") {
        cursor = 0;
        render();
      } else if (key.name === "end") {
        cursor = selectableIndexes.length - 1;
        render();
      } else if (key.name === "space") {
        toggleSelection(cursor);
        render();
      } else if (key.name === "a") {
        selectableIndexes.forEach((index) => selected.add(index));
        addAssociatedDeltas(selected, associatedDeltaIndexes);
        render();
      } else if (key.name === "i") {
        selectableIndexes.forEach((index) => {
          if (selected.has(index)) {
            selected.delete(index);
          } else {
            selected.add(index);
          }
        });
        addAssociatedDeltas(selected, associatedDeltaIndexes);
        render();
      } else if (key.name === "return" || key.name === "enter") {
        addAssociatedDeltas(selected, associatedDeltaIndexes);
        cleanup();
        resolve(
          Array.from(selected)
            .sort((a, b) => a - b)
            .map((i) => candidates[i])
        );
      } else if (key.name === "q" || (key.ctrl && key.name === "c")) {
        cleanup();
        resolve([]);
      }
    };
    const cleanup = () => {
      process.stdin.off("keypress", onKeypress);
      process.stdin.setRawMode(wasRaw);
      process.stdout.write("\x1Bc");
    };
    process.stdin.on("keypress", onKeypress);
  });

  return result;
}

function printDeletionSummary(selected: CleanupCandidate[]): void {
  log(
    `Selected ${selected.length} object(s) for deletion. ` +
      formatCleanupSpaceEstimate(selected)
  );
  for (const obj of selected) {
    console.log(
      `  ${chalk.red("delete")} ${obj.key} ` +
        chalk.dim(`(${obj.version}, ${formatBytes(obj.size)})`)
    );
  }
}

function confirmDeletion(selected: CleanupCandidate[]): Promise<boolean> {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  });
  return new Promise((resolve) => {
    rl.question(chalk.red(formatCleanupConfirmation(selected)), (answer) => {
      rl.close();
      const normalized = answer.trim().toLowerCase();
      resolve(normalized === "" || normalized === "y" || normalized === "yes");
    });
  });
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function shellEscape(s: string): string {
  return /^[\w.,/=:+@-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`;
}

const CONTENT_TYPES: Record<string, string> = {
  ".dmg": "application/x-apple-diskimage",
  ".zip": "application/zip",
  ".xml": "application/xml",
  ".json": "application/json",
  ".txt": "text/plain; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "application/javascript",
  ".pkg": "application/octet-stream",
  ".tar": "application/x-tar",
  ".gz": "application/gzip",
  ".tgz": "application/gzip",
  ".sig": "application/octet-stream",
  // Sparkle binary delta patches (BinaryDelta archives produced by
  // generate_appcast). Served as octet-stream so browsers / proxies
  // don't try to sniff or transform them.
  ".delta": "application/octet-stream",
};

function guessContentType(filePath: string): string | null {
  return CONTENT_TYPES[path.extname(filePath).toLowerCase()] || null;
}
