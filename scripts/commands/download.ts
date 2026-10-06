import { Command } from "commander";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import {
  ENGINE_DIR,
  ROOT_DIR,
  loadConfig,
  log,
  success,
  error,
  warn,
  which,
  run,
  runStreaming,
  resolveBuildTarget,
} from "../utils.js";

const WINDOWS_CHECKOUT_MARKER = '# Dao: retain Windows dependencies for cross compilation.';

async function syncChromium(args: string[], crossWindows: boolean): Promise<number> {
  if (crossWindows) {
    const configured = await runStreaming('python3', [
      path.join(ROOT_DIR, 'scripts', 'configure-windows-checkout.py'),
      path.join(ENGINE_DIR, '.gclient'),
    ]);
    if (configured !== 0) return configured;
  }
  const code = await runStreaming('gclient', [
    ...args, ...(crossWindows ? ['--nohooks'] : []),
  ], {cwd: ENGINE_DIR});
  if (code !== 0 || !crossWindows) return code;
  // Read the pinned SDK hash only after the requested Chromium checkout exists.
  const {getWindowsToolchainEnvironment} = await import('../windows-toolchain.js');
  return runStreaming('gclient', ['runhooks'], {
    cwd: ENGINE_DIR, env: getWindowsToolchainEnvironment(),
  });
}

export const downloadCommand = new Command("download")
  .description("Fetch Chromium source at the version specified in dao.json")
  .option('--platform <platform>', 'Target platform: mac or windows (default: host or DAO_BUILD_PLATFORM)')
  .option("--force", "Re-download even if engine/ already exists")
  .option("--full-history", "Clone with full git history (default is shallow)")
  .action(async (opts: { force?: boolean; fullHistory?: boolean; platform?: string }) => {
    const config = loadConfig();
    const target = resolveBuildTarget(config, process.platform, process.arch, opts.platform);
    const gclientFile = path.join(ENGINE_DIR, '.gclient');
    // Once this checkout includes Windows, subsequent native Mac syncs must
    // retain its dependencies and use the same private SDK archive for hooks.
    const crossWindows = process.platform === 'darwin' && (target.os === 'win' ||
      (existsSync(gclientFile) && readFileSync(gclientFile, 'utf8').includes(WINDOWS_CHECKOUT_MARKER)));
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

    if (crossWindows) {
      // Reject a missing SDK configuration before downloading any source.
      const {getWindowsToolchainEnvironment} = await import('../windows-toolchain.js');
      getWindowsToolchainEnvironment();
    }

    if (existsSync(gclientFile) && !opts.force) {
      warn("engine/ already exists. Use --force to re-download.");
      log("Running gclient sync to update...");

      let revision = `refs/tags/${version}`;
      if (shallow) {
        // gclient only skips its full-history fetch of every branch when the
        // revision is a SHA that already exists locally, so fetch it first.
        const srcDir = path.join(ENGINE_DIR, "src");
        const fetchCode = await runStreaming("git", [
          "-C", srcDir, "fetch", "--depth=1", "--no-tags", "origin", `+${revision}:${revision}`,
        ]);
        if (fetchCode !== 0) {
          error(`Failed to fetch ${revision}`);
          process.exit(1);
        }
        revision = run(`git -C "${srcDir}" rev-parse "${revision}^{commit}"`, { silent: true });
      }

      const syncArgs = [
        "sync",
        "--revision", `src@${revision}`,
        ...(shallow ? ["--no-history", "--shallow"] : ["--with_branch_heads", "--with_tags"]),
      ];
      const syncCode = await syncChromium(syncArgs, crossWindows);

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

    const syncArgs = [
      "sync",
      ...(shallow ? ["--no-history", "--shallow"] : ["--with_branch_heads", "--with_tags"]),
    ];
    const code = await syncChromium(syncArgs, crossWindows);

    if (code !== 0) {
      error("Failed to fetch Chromium source");
      process.exit(1);
    }

    success(`Chromium ${version} downloaded to engine/`);
  });
