import { Command } from "commander";
import {createHash} from "node:crypto";
import { spawnSync } from "node:child_process";
import {
  existsSync,
  mkdtempSync,
  readFileSync,
  rmSync,
} from "node:fs";
import os from "node:os";
import path from "node:path";

import {
  ROOT_DIR,
  error,
  log,
  success,
  which,
  runStreaming,
} from "../utils.js";
import {windowsInstallerName} from "./package-windows.js";

const RELEASE_BASE_URL = "https://dao-release.msgbyte.com";

export interface GithubReleasePlan {
  tag: string;
  assetName: string;
  sourceUrl: string;
}

interface GithubReleaseOptions {
  dryRun?: boolean;
  platform?: "mac" | "windows";
  asset?: string;
}

interface GithubReleaseBackfillOptions extends GithubReleaseOptions {
  appcast: string;
}

export function buildGithubReleasePlan(tag: string, platform: "mac" | "windows" = "mac"): GithubReleasePlan {
  if (!/^v\d+\.\d+\.\d+$/.test(tag)) {
    throw new Error(`Invalid release tag: ${tag}`);
  }
  const version = tag.slice(1);
  const assetName = platform === "windows" ? windowsInstallerName(version) : `dao-browser-${version}-mac-arm64.dmg`;
  return {
    tag,
    assetName,
    sourceUrl: `${RELEASE_BASE_URL}/${assetName}`,
  };
}

export function collectGithubReleasePlans(xml: string): GithubReleasePlan[] {
  const plans: GithubReleasePlan[] = [];
  const seen = new Set<string>();
  const stripped = xml.replace(/<!--[\s\S]*?-->/g, "");
  for (const match of stripped.matchAll(/<enclosure\b([^>]*?)\/>/g)) {
    const attributes = match[1];
    if (/\bsparkle:deltaFrom\s*=/.test(attributes)) continue;
    const encodedUrl = attributes.match(/\burl="([^"]+)"/)?.[1];
    if (!encodedUrl) continue;
    const sourceUrl = encodedUrl.replaceAll("&amp;", "&");
    let assetName: string;
    try {
      assetName = decodeURIComponent(
        path.posix.basename(new URL(sourceUrl).pathname)
      );
    } catch {
      continue;
    }
    const version = assetName.match(
      /^dao-browser-(\d+\.\d+\.\d+)-mac-[\w-]+\.dmg$/
    )?.[1];
    if (!version || seen.has(version)) continue;
    seen.add(version);
    plans.push({ tag: `v${version}`, assetName, sourceUrl });
  }
  return plans;
}

export const githubReleaseCommand = new Command("github-release")
  .description("Archive release DMGs on GitHub Releases");

githubReleaseCommand
  .command("publish")
  .description("Publish one tagged DMG to GitHub Releases")
  .argument("<tag>", "Release tag, for example v1.0.101")
  .option("--platform <platform>", "Desktop platform: mac | windows", (value: string) => {
    if (value !== "mac" && value !== "windows") throw new Error(`Invalid --platform: ${value}`);
    return value;
  }, "mac")
  .option("--asset <path>", "Attach a local installer and its SHA-256 checksum")
  .option("--dry-run", "Print the archive operation without running it")
  .action(async (tag: string, options: GithubReleaseOptions) => {
    try {
      if (options.platform === "windows" && !options.asset) {
        throw new Error("Windows publishing requires --asset pointing to the packaged installer.");
      }
      await publishGithubRelease(buildGithubReleasePlan(tag, options.platform), options);
    } catch (cause) {
      error(cause instanceof Error ? cause.message : String(cause));
      process.exitCode = 1;
    }
  });

githubReleaseCommand
  .command("backfill")
  .description("Publish every DMG referenced by the website appcast")
  .option(
    "--appcast <path>",
    "Appcast to archive",
    path.join(ROOT_DIR, "website/public/appcast.xml")
  )
  .option("--dry-run", "Print archive operations without running them")
  .action(async (options: GithubReleaseBackfillOptions) => {
    try {
      const appcastPath = path.resolve(options.appcast);
      if (!existsSync(appcastPath)) {
        throw new Error(`Appcast not found: ${appcastPath}`);
      }
      const plans = collectGithubReleasePlans(
        readFileSync(appcastPath, "utf-8")
      );
      log(`Archiving ${plans.length} appcast release(s) on GitHub`);
      for (const plan of plans) {
        await publishGithubRelease(plan, options);
      }
    } catch (cause) {
      error(cause instanceof Error ? cause.message : String(cause));
      process.exitCode = 1;
    }
  });

interface GithubReleaseState {
  exists: boolean;
  assetExists: boolean;
  isDraft: boolean;
  checksumExists?: boolean;
}

function inspectGithubRelease(plan: GithubReleasePlan): GithubReleaseState {
  const result = spawnSync(
    "gh",
    [
      "release",
      "view",
      plan.tag,
      "--json",
      "assets,isDraft",
    ],
    { cwd: ROOT_DIR, encoding: "utf-8", stdio: ["ignore", "pipe", "pipe"] }
  );
  if (result.error) throw result.error;
  if (result.status !== 0) {
    if (!/not found|HTTP 404/i.test(result.stderr)) {
      throw new Error(`Cannot inspect GitHub Release ${plan.tag}: ${result.stderr.trim()}`);
    }
    return { exists: false, assetExists: false, isDraft: false };
  }
  const release = JSON.parse(result.stdout) as {
    assets?: Array<{ name?: string }>;
    isDraft?: boolean;
  };
  return {
    exists: true,
    assetExists:
      release.assets?.some((asset) => asset.name === plan.assetName) ?? false,
    isDraft: release.isDraft === true,
    checksumExists: release.assets?.some(asset => asset.name === plan.assetName + ".sha256") ?? false,
  };
}

async function runCommand(command: string, args: string[]): Promise<void> {
  const code = await runStreaming(command, args, {cwd: ROOT_DIR});
  if (code !== 0) throw new Error(`${command} exited with code ${code}`);
}

export function validateLocalInstaller(plan: GithubReleasePlan, asset: string): string {
  const assetPath = path.resolve(asset);
  if (path.basename(assetPath) !== plan.assetName || !plan.assetName.endsWith(".exe")) {
    throw new Error(`Expected Windows installer named ${plan.assetName}`);
  }
  const checksum = readFileSync(assetPath + ".sha256", "utf8");
  const digest = createHash("sha256").update(readFileSync(assetPath)).digest("hex");
  if (checksum !== `${digest}  ${plan.assetName}\n`) {
    throw new Error("Installer checksum mismatch; package the installer again.");
  }
  return assetPath;
}

async function publishLocalInstaller(plan: GithubReleasePlan, options: GithubReleaseOptions): Promise<void> {
  if (options.dryRun) {
    console.log(`[dry-run] Attach ${options.asset} and its .sha256 to desktop GitHub Release ${plan.tag}`);
    return;
  }
  const assetPath = validateLocalInstaller(plan, options.asset!);
  if (!which("gh")) throw new Error("gh CLI not found; install GitHub CLI and run gh auth login.");
  const state = inspectGithubRelease(plan);
  if (state.isDraft) {
    throw new Error(`Desktop release ${plan.tag} is still a draft. Publish the macOS desktop release before attaching Windows assets.`);
  }
  if (state.exists) {
    // Never replace already published assets, including the macOS DMG.
    if (state.assetExists || state.checksumExists) {
      const tempDir = mkdtempSync(path.join(os.tmpdir(), "dao-verify-release-"));
      try {
        const remote = path.join(tempDir, "existing");
        await runCommand("gh", ["release", "download", plan.tag, "--pattern",
          state.checksumExists ? plan.assetName + ".sha256" : plan.assetName, "--output", remote]);
        const expected = readFileSync(assetPath + ".sha256", "utf8");
        const actual = state.checksumExists ? readFileSync(remote, "utf8")
          : `${createHash("sha256").update(readFileSync(remote)).digest("hex")}  ${plan.assetName}\n`;
        if (actual !== expected) throw new Error("A different installer is already published for this version; refusing to replace it.");
      } finally {
        rmSync(tempDir, {recursive: true, force: true});
      }
    }
    if (!state.assetExists) {
      await runCommand("gh", ["release", "upload", plan.tag, assetPath]);
    }
    if (!state.checksumExists) {
      await runCommand("gh", ["release", "upload", plan.tag, assetPath + ".sha256"]);
    }
  } else {
    await runCommand("gh", ["release", "create", plan.tag, assetPath, assetPath + ".sha256",
      "--verify-tag", "--title", `Dao Browser ${plan.tag}`, "--generate-notes"]);
  }
  success(`${plan.tag} contains ${plan.assetName}`);
}

export async function publishGithubRelease(
  plan: GithubReleasePlan,
  options: GithubReleaseOptions = {}
): Promise<void> {
  if (options.asset) {
    await publishLocalInstaller(plan, options);
    return;
  }
  if (options.dryRun) {
    console.log(`[dry-run] curl ${plan.sourceUrl}`);
    console.log(
      `[dry-run] gh release create ${plan.tag} ${plan.assetName} ` +
        `--verify-tag --title "Dao Browser ${plan.tag}" --generate-notes`
    );
    return;
  }
  if (!which("gh")) throw new Error("gh CLI not found");
  if (!which("curl")) throw new Error("curl not found");

  const state = inspectGithubRelease(plan);
  if (state.assetExists) {
    if (state.isDraft) {
      await runCommand("gh", ["release", "edit", plan.tag, "--draft=false"]);
    }
    success(`${plan.tag} already contains ${plan.assetName}`);
    return;
  }

  const tempDir = mkdtempSync(path.join(os.tmpdir(), "dao-github-release-"));
  const assetPath = path.join(tempDir, plan.assetName);
  try {
    log(`Downloading ${plan.sourceUrl}`);
    await runCommand("curl", [
      "--fail",
      "--location",
      "--retry",
      "3",
      "--output",
      assetPath,
      plan.sourceUrl,
    ]);

    if (state.exists) {
      await runCommand("gh", ["release", "upload", plan.tag, assetPath]);
      if (state.isDraft) {
        await runCommand("gh", ["release", "edit", plan.tag, "--draft=false"]);
      }
    } else {
      await runCommand("gh", [
        "release",
        "create",
        plan.tag,
        assetPath,
        "--verify-tag",
        "--title",
        `Dao Browser ${plan.tag}`,
        "--generate-notes",
      ]);
    }
    success(`Archived ${plan.assetName} in GitHub Release ${plan.tag}`);
  } finally {
    rmSync(tempDir, { recursive: true, force: true });
  }
}
