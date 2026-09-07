import {execFile} from "node:child_process";
import {readFile, writeFile} from "node:fs/promises";
import path from "node:path";
import {createInterface} from "node:readline/promises";
import {promisify} from "node:util";
import {Command} from "commander";
import {ROOT_DIR, error, log, success} from "../utils.js";

const exec = promisify(execFile);
const VERSION_FILE = "android/app/build.gradle.kts";
const VERSION_NAME = /^(\s*versionName\s*=\s*)"([^"\r\n]+)"[ \t]*$/m;
const VERSION_CODE = /^(\s*versionCode\s*=\s*)(\d+)[ \t]*$/m;

export interface AndroidVersion {
  version: string;
  versionCode: number;
}

interface ReleaseOptions {
  version?: string;
  versionCode?: string;
  dryRun?: boolean;
}

function parts(version: string): number[] {
  if (!/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version)) {
    throw new Error("Version must use major.minor.patch, for example 0.1.1.");
  }
  const result = version.split(".").map(Number);
  if (result.some((part) => !Number.isSafeInteger(part))) throw new Error("Version is too large.");
  return result;
}

export function validateAndroidVersion(next: AndroidVersion, current?: AndroidVersion): void {
  const nextParts = parts(next.version);
  if (!Number.isInteger(next.versionCode) || next.versionCode < 1 || next.versionCode > 2_100_000_000) {
    throw new Error("Version code must be an integer from 1 to 2100000000.");
  }
  if (current) {
    const currentParts = parts(current.version);
    const difference = nextParts.map((part, index) => part - currentParts[index]).find((part) => part !== 0) ?? 0;
    if (difference <= 0 || next.versionCode <= current.versionCode) {
      throw new Error(`Version and version code must increase from ${current.version} (${current.versionCode}).`);
    }
  }
}

export async function resolveAndroidVersion(
  current: AndroidVersion,
  options: ReleaseOptions,
  ask?: (message: string) => Promise<string>,
): Promise<AndroidVersion> {
  if ((!options.version || !options.versionCode) && !ask) {
    throw new Error("Pass --version and --version-code when not running in an interactive terminal.");
  }
  const [major, minor, patch] = parts(current.version);
  const suggested = `${major}.${minor}.${patch + 1}`;
  const version = options.version ?? ((await ask!(`Release version (${suggested}): `)).trim() || suggested);
  const code = options.versionCode ?? ((await ask!(`Version code (${current.versionCode + 1}): `)).trim() || String(current.versionCode + 1));
  if (!/^[1-9]\d*$/.test(code)) throw new Error("Version code must be a positive decimal integer.");
  const result = {version, versionCode: Number(code)};
  validateAndroidVersion(result, current);
  return result;
}

export async function runAndroidRelease(options: ReleaseOptions, root = ROOT_DIR): Promise<void> {
  const file = path.join(root, VERSION_FILE);
  const source = await readFile(file, "utf8");
  const name = source.match(VERSION_NAME);
  const code = source.match(VERSION_CODE);
  if (!name || !code) throw new Error(`Cannot read literal Android versions from ${VERSION_FILE}.`);
  const current = {version: name[2], versionCode: Number(code[2])};
  validateAndroidVersion(current);
  log(`Current Android version: ${current.version} (versionCode: ${current.versionCode})`);
  const terminal = process.stdin.isTTY && process.stdout.isTTY && !process.env.CI
    ? createInterface({input: process.stdin, output: process.stdout}) : undefined;
  let next: AndroidVersion;
  try {
    next = await resolveAndroidVersion(current, options, terminal && ((message) => terminal.question(message)));
  } finally {
    terminal?.close();
  }
  const tag = `android-v${next.version}`;
  const message = `chore(android): release ${next.version}`;
  const push = ["push", "--atomic", "--no-follow-tags", "origin", "refs/heads/main:refs/heads/main", `refs/tags/${tag}:refs/tags/${tag}`];
  const pushCommand = `git ${push.join(" ")}`;
  log(`${VERSION_FILE}: versionName: ${current.version} -> ${next.version}, versionCode: ${current.versionCode} -> ${next.versionCode}`);
  log(`Commit: ${message}`);
  log(`Tag: ${tag}`);
  log(pushCommand);
  log("The tag workflow builds the signed APK and publishes its GitHub Release.");
  if (options.dryRun) return;

  const git = async (...args: string[]) => {
    try {
      return (await exec("git", args, {cwd: root, maxBuffer: 4 * 1024 * 1024})).stdout.trim();
    } catch (cause) {
      const failure = cause as Error & {stderr?: string};
      throw new Error(`git ${args[0]} failed: ${failure.stderr?.trim() || failure.message}`);
    }
  };
  if (await git("branch", "--show-current") !== "main" || await git("status", "--porcelain")) {
    throw new Error("Run releases from a clean main checkout. Commit or finish your existing changes first.");
  }
  if (await git("tag", "--list", tag)) throw new Error(`Local tag ${tag} already exists.`);
  const refs = await git("ls-remote", "origin", "refs/heads/main", `refs/tags/${tag}`);
  const remoteMain = refs.split("\n").find((line) => line.endsWith("\trefs/heads/main"))?.split("\t")[0];
  if (refs.split("\n").some((line) => line.endsWith(`\trefs/tags/${tag}`))) {
    throw new Error(`Remote tag ${tag} already exists.`);
  }
  if (!remoteMain) throw new Error("origin must have a main branch before releasing.");
  try {
    await git("merge-base", "--is-ancestor", remoteMain, "HEAD");
  } catch {
    throw new Error("Local main does not contain remote main. Synchronize your checkout before releasing.");
  }
  await git("var", "GIT_AUTHOR_IDENT");
  await git("var", "GIT_COMMITTER_IDENT");
  const updated = source.replace(VERSION_NAME, (_, prefix) => `${prefix}"${next.version}"`)
    .replace(VERSION_CODE, (_, prefix) => `${prefix}${next.versionCode}`);
  await writeFile(file, updated);
  try {
    // --only keeps unrelated staged files out of the release commit.
    await git("commit", "--only", "-m", message, "--", VERSION_FILE);
  } catch (cause) {
    throw new Error(`${cause}\nVersion edits remain in ${VERSION_FILE}; finish or undo them before retrying.`);
  }
  try {
    await git("tag", tag, "HEAD");
  } catch (cause) {
    throw new Error(`${cause}\nThe release commit is local. Inspect it, then create ${tag} before pushing.`);
  }
  try {
    await git(...push);
  } catch (cause) {
    throw new Error(`${cause}\nThe local release commit and tag are preserved. Resolve the push failure and retry:\n${pushCommand}`);
  }
  success(`Pushed ${tag}. Follow the Publish Android GitHub Release workflow on GitHub.`);
}

export const releaseAndroidCommand = new Command("release-android")
  .description("Bump Android versions, commit, tag, and push for a workflow-built release")
  .option("--version <version>", "Android version name (prompt when omitted)")
  .option("--version-code <code>", "Increasing Android version code (prompt when omitted)")
  .option("--dry-run", "Preview version and Git operations without changes or network access")
  .action(async (options: ReleaseOptions) => {
    try { await runAndroidRelease(options); }
    catch (cause) { error(cause instanceof Error ? cause.message : String(cause)); process.exitCode = 1; }
  });
