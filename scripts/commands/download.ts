import { Command } from "commander";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import {
  ENGINE_DIR,
  loadConfig,
  log,
  success,
  error,
  warn,
  which,
  run,
  runStreaming,
} from "../utils.js";

export const downloadCommand = new Command("download")
  .description("Fetch Chromium source at the version specified in dao.json")
  .option("--force", "Re-download even if engine/ already exists")
  .option("--full-history", "Clone with full git history (default is shallow)")
  .action(async (opts: { force?: boolean; fullHistory?: boolean }) => {
    const config = loadConfig();
    const version = config.version.version;
    const shallow = !opts.fullHistory;

    log(`Chromium version: ${version}`);
    if (shallow) {
      log("Using shallow clone (use --full-history for complete git history)");
    }

    if (!which("gclient")) {
      error("depot_tools not found in PATH.");
      console.log(`
Install depot_tools first:
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
  export PATH="$PATH:/path/to/depot_tools"

See: https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html
`);
      process.exit(1);
    }

    if (existsSync(path.join(ENGINE_DIR, ".gclient")) && !opts.force) {
      warn("engine/ already exists. Use --force to re-download.");
      log("Running gclient sync to update...");

      const syncArgs = [
        "sync",
        "--revision", `src@refs/tags/${version}`,
        ...(shallow ? ["--no-history", "--shallow"] : ["--with_branch_heads", "--with_tags"]),
      ];
      const syncCode = await runStreaming("gclient", syncArgs, { cwd: ENGINE_DIR });

      if (syncCode !== 0) {
        error("gclient sync failed");
        process.exit(1);
      }

      success("Chromium source updated");
      return;
    }

    // Clean up leftover artifacts from previous failed attempts
    const badScmDir = path.join(ENGINE_DIR, "_bad_scm");
    if (existsSync(badScmDir)) {
      log("Cleaning up leftover _bad_scm directory...");
      const { rmSync } = await import("node:fs");
      rmSync(badScmDir, { recursive: true, force: true });
    }

    log("Fetching Chromium source (this will take a while)...");
    mkdirSync(ENGINE_DIR, { recursive: true });

    writeFileSync(
      path.join(ENGINE_DIR, ".gclient"),
      `solutions = [
  {
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git@refs/tags/${version}",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
      "checkout_nacl": False,
      "checkout_android": False,
      "checkout_android_native_support": False,
      "checkout_ios_webkit": False,
      "checkout_openxr": False,
    },
  },
]
`
    );

    // Increase git buffer size to reduce connection drops on large repos
    try {
      run("git config --global http.postBuffer 524288000", { silent: true });
    } catch {
      // non-critical
    }

    const syncArgs = [
      "sync",
      ...(shallow ? ["--no-history", "--shallow"] : ["--with_branch_heads", "--with_tags"]),
    ];
    const code = await runStreaming("gclient", syncArgs, { cwd: ENGINE_DIR });

    if (code !== 0) {
      error("Failed to fetch Chromium source");
      process.exit(1);
    }

    success(`Chromium ${version} downloaded to engine/`);
  });
