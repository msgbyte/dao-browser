import { Command } from "commander";
import { spawn, spawnSync } from "node:child_process";
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
} from "../utils.js";

const RELEASE_BASE_URL = "https://dao-release.msgbyte.com";

export interface GithubReleasePlan {
  tag: string;
  assetName: string;
  sourceUrl: string;
}

interface GithubReleaseOptions {
  dryRun?: boolean;
}

interface GithubReleaseBackfillOptions extends GithubReleaseOptions {
  appcast: string;
}

export function buildGithubReleasePlan(tag: string): GithubReleasePlan {
  if (!/^v\d+\.\d+\.\d+$/.test(tag)) {
    throw new Error(`Invalid release tag: ${tag}`);
  }
  const version = tag.slice(1);
  const assetName = `dao-browser-${version}-mac-arm64.dmg`;
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
  .option("--dry-run", "Print the archive operation without running it")
  .action(async (tag: string, options: GithubReleaseOptions) => {
    try {
      await publishGithubRelease(buildGithubReleasePlan(tag), options);
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
  };
}

function runCommand(command: string, args: string[]): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: ROOT_DIR,
      env: process.env,
      stdio: "inherit",
    });
    child.once("error", reject);
    child.once("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`${command} exited with code ${code}`));
      }
    });
  });
}

export async function publishGithubRelease(
  plan: GithubReleasePlan,
  options: GithubReleaseOptions = {}
): Promise<void> {
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
