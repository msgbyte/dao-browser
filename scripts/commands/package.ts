import { Command } from "commander";
import { spawn } from "node:child_process";
import {
  existsSync,
  mkdirSync,
  rmSync,
  cpSync,
  mkdtempSync,
  readdirSync,
  statSync,
  lstatSync,
} from "node:fs";
import os from "node:os";
import path from "node:path";
import chalk from "chalk";
import {
  ENGINE_DIR,
  ROOT_DIR,
  loadConfig,
  log,
  success,
  warn,
  error,
  run,
  runStreaming,
  which,
} from "../utils.js";
import { getAppName } from "./build.js";

const DIST_DIR = path.join(ROOT_DIR, "dist");
const ENTITLEMENTS = path.join(
  ROOT_DIR,
  "branding",
  "mac",
  "dao.entitlements"
);
const HELPER_ENTITLEMENTS = path.join(
  ROOT_DIR,
  "branding",
  "mac",
  "dao_helper.entitlements"
);

interface PackageOptions {
  zip?: boolean;
  sign?: boolean;
  signId?: boolean;
  notarize?: boolean;
  staple?: boolean;
  debug?: boolean;
}

export const packageCommand = new Command("package")
  .description("Package Dao Browser into a distributable .dmg or .zip")
  .option("--zip", "Create a .zip archive instead of .dmg")
  .option("--sign", "Apply ad-hoc code signature (no certificate required)")
  .option(
    "--sign-id",
    "Sign with Developer ID Application certificate (reads $DAO_SIGN_IDENTITY)"
  )
  .option(
    "--notarize",
    "Submit the artifact to Apple's notary service (requires --sign-id)"
  )
  .option(
    "--staple",
    "Staple the notarization ticket onto the artifact (requires --notarize)"
  )
  .option(
    "--debug",
    "Package the debug build (out/dao-debug/) instead of the release build"
  )
  .action(async (opts: PackageOptions) => {
    const config = loadConfig();
    // Debug builds carry a " Debug" suffix in their product/app bundle name
    // (set by syncMacBranding in build.ts) so they can coexist with release.
    const appName = getAppName(config.display_name, !!opts.debug);
    const version = config.version.display;
    const srcDir = path.join(ENGINE_DIR, "src");
    const outDirName = opts.debug ? "dao-debug" : "dao";
    const outDir = path.join(srcDir, "out", outDirName);
    const appBundle = path.join(outDir, `${appName}.app`);

    if (!existsSync(appBundle)) {
      error(
        `${appName}.app not found at ${appBundle}. Run 'npm run build' first.`
      );
      process.exit(1);
    }

    if (opts.notarize && !opts.signId) {
      error("--notarize requires --sign-id (notarization needs a real signature).");
      process.exit(1);
    }
    if (opts.staple && !opts.notarize) {
      error("--staple requires --notarize.");
      process.exit(1);
    }
    if (opts.sign && opts.signId) {
      error("Use either --sign (ad-hoc) or --sign-id (Developer ID), not both.");
      process.exit(1);
    }

    mkdirSync(DIST_DIR, { recursive: true });

    if (opts.sign) {
      log("Applying ad-hoc code signature...");
      try {
        run(`codesign --force --sign - --deep "${appBundle}"`, {
          silent: true,
        });
        success("Ad-hoc signed");
      } catch {
        warn("Code signing failed (non-critical, continuing)");
      }
    } else if (opts.signId) {
      const identity = process.env.DAO_SIGN_IDENTITY;
      if (!identity) {
        error(
          "DAO_SIGN_IDENTITY is not set.\n" +
            'Example: export DAO_SIGN_IDENTITY="Developer ID Application: Foo Bar (TEAMID1234)"'
        );
        process.exit(1);
      }
      signAppBundle(appBundle, identity);
      verifyCodesign(appBundle);
    }

    const arch = config.build.target_cpu;
    const baseName = `dao-browser-${version}-mac-${arch}`;

    let artifactPath: string;
    if (opts.zip) {
      artifactPath = await createZip(appBundle, appName, baseName);
    } else {
      artifactPath = await createDmg(appBundle, appName, baseName);
      // The .dmg itself must also be signed for notarization to succeed.
      if (opts.signId) {
        const identity = process.env.DAO_SIGN_IDENTITY!;
        log("Signing the .dmg ...");
        run(
          `codesign --force --timestamp --sign "${identity}" "${artifactPath}"`,
          { silent: true }
        );
        success("DMG signed");
      }
    }

    if (opts.notarize) {
      await notarize(artifactPath);
    }
    if (opts.staple) {
      await staple(artifactPath);
      assessArtifact(artifactPath);
    }
  });

async function createZip(
  appBundle: string,
  appName: string,
  baseName: string
): Promise<string> {
  const zipPath = path.join(DIST_DIR, `${baseName}.zip`);
  if (existsSync(zipPath)) rmSync(zipPath);

  log(`Creating ${baseName}.zip ...`);
  run(`ditto -c -k --sequesterRsrc --keepParent "${appBundle}" "${zipPath}"`);
  success(`Created: dist/${baseName}.zip`);
  return zipPath;
}

async function createDmg(
  appBundle: string,
  appName: string,
  baseName: string
): Promise<string> {
  const dmgPath = path.join(DIST_DIR, `${baseName}.dmg`);
  if (existsSync(dmgPath)) rmSync(dmgPath);

  const createDmgBin = await ensureCreateDmg();

  log(`Creating ${baseName}.dmg ...`);

  const iconPath = path.join(ROOT_DIR, "branding", "mac", "app.icns");
  const stageDir = mkdtempSync(path.join(os.tmpdir(), "dao-dmg-stage-"));
  try {
    // create-dmg packages an entire source folder; stage only the .app so
    // nothing extraneous leaks into the disk image.
    cpSync(appBundle, path.join(stageDir, `${appName}.app`), {
      recursive: true,
      verbatimSymlinks: true,
    });

    const buildArgs = (skipFinderStyling: boolean): string[] => {
      const a = [
        "--volname", appName,
        ...(existsSync(iconPath) ? ["--volicon", iconPath] : []),
        "--window-size", "540", "380",
        "--icon-size", "80",
        "--icon", `${appName}.app`, "140", "190",
        "--app-drop-link", "400", "190",
        "--format", "UDZO",
        "--hdiutil-quiet",
        "--no-internet-enable",
      ];
      if (skipFinderStyling) a.push("--skip-jenkins");
      a.push(dmgPath, stageDir);
      return a;
    };

    // First attempt: full visual styling (requires Finder Apple-event access).
    let result = await spawnCapture(createDmgBin, buildArgs(false));

    if (!result.ok && isFinderAccessDenied(result.stderr)) {
      warn(
        "macOS denied AppleScript access to Finder (TCC -1743).\n" +
          "  Finder window styling (icon positions, window size) will be skipped.\n" +
          "  To enable styling: System Settings > Privacy & Security > Automation,\n" +
          "  grant your terminal app permission to control Finder."
      );
      // Clean up any partial DMG from the failed first pass before retrying.
      if (existsSync(dmgPath)) rmSync(dmgPath);
      result = await spawnCapture(createDmgBin, buildArgs(true));
    }

    if (!result.ok) {
      error(`create-dmg exited with code ${result.code}.`);
      process.exit(1);
    }

    success(`Created: dist/${baseName}.dmg`);
    return dmgPath;
  } finally {
    rmSync(stageDir, { recursive: true, force: true });
  }
}

interface SpawnResult {
  ok: boolean;
  code: number | null;
  stderr: string;
}

// Run a command, streaming stdout/stderr live while also capturing stderr so
// callers can inspect it for known failure patterns.
function spawnCapture(cmd: string, args: string[]): Promise<SpawnResult> {
  console.log(chalk.dim(`$ ${cmd} ${args.map(shellEscape).join(" ")}`));
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { stdio: ["ignore", "inherit", "pipe"] });
    let stderr = "";
    child.stderr?.on("data", (chunk: Buffer) => {
      const s = chunk.toString();
      stderr += s;
      process.stderr.write(s);
    });
    child.on("error", (err: Error) =>
      resolve({ ok: false, code: null, stderr: stderr + String(err) })
    );
    child.on("close", (code: number | null) =>
      resolve({ ok: code === 0, code, stderr })
    );
  });
}

function shellEscape(s: string): string {
  return /^[\w.,/=:+-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`;
}

function isFinderAccessDenied(stderr: string): boolean {
  return (
    /-1743/.test(stderr) ||
    /Failed running AppleScript/i.test(stderr) ||
    /not authori[sz]ed to send Apple events/i.test(stderr) ||
    /未获得授权将Apple事件/.test(stderr)
  );
}

async function ensureCreateDmg(): Promise<string> {
  const existing = which("create-dmg");
  if (existing) return existing;

  const brew = which("brew");
  if (!brew) {
    error(
      "create-dmg not found in PATH and Homebrew is not installed.\n" +
        "Install Homebrew from https://brew.sh, then run:\n" +
        "  brew install create-dmg\n" +
        "Or pass --zip to build a .zip archive instead."
    );
    process.exit(1);
  }

  warn("create-dmg not found, installing via Homebrew...");
  const code = await runStreaming("brew", ["install", "create-dmg"]);
  if (code !== 0) {
    error(
      `'brew install create-dmg' failed with exit code ${code}.\n` +
        "Install it manually, or pass --zip to build a .zip archive instead."
    );
    process.exit(1);
  }

  const installed = which("create-dmg");
  if (!installed) {
    error(
      "create-dmg still not found after install. Check your Homebrew PATH."
    );
    process.exit(1);
  }
  success("create-dmg installed");
  return installed;
}

// ---------------------------------------------------------------------------
// Code signing (Developer ID + Hardened Runtime)
// ---------------------------------------------------------------------------

/**
 * Sign every nested signable item (helpers, frameworks, dylibs) inside the app
 * bundle, then sign the outer app.
 *
 * Apple recommends against `codesign --deep`. We walk the tree ourselves:
 *   - Sign deepest items first (helpers / frameworks / dylibs / .so).
 *   - Use the helper-specific entitlements for *.app under the main app.
 *   - Use the main entitlements only for the outer Dao.app.
 */
function signAppBundle(appBundle: string, identity: string): void {
  if (!existsSync(ENTITLEMENTS)) {
    error(`Entitlements not found: ${ENTITLEMENTS}`);
    process.exit(1);
  }
  if (!existsSync(HELPER_ENTITLEMENTS)) {
    error(`Helper entitlements not found: ${HELPER_ENTITLEMENTS}`);
    process.exit(1);
  }

  log(`Signing ${path.basename(appBundle)} with identity: ${identity}`);

  const items = collectSignables(appBundle);
  // Sort by depth descending so children sign before parents.
  items.sort((a, b) => depth(b) - depth(a));

  for (const item of items) {
    const ent = isAppBundle(item) ? HELPER_ENTITLEMENTS : null;
    signOne(item, identity, ent);
  }

  // Outer app last.
  signOne(appBundle, identity, ENTITLEMENTS);
  success("App bundle signed");
}

function collectSignables(root: string): string[] {
  const out: string[] = [];
  walk(root, (p) => {
    // Avoid following symlinks; codesign handles them via their parent.
    let st: ReturnType<typeof lstatSync>;
    try {
      st = lstatSync(p);
    } catch {
      return false;
    }
    if (st.isSymbolicLink()) return false;

    if (p === root) return true; // descend, don't sign yet
    const base = path.basename(p);
    if (
      base.endsWith(".app") ||
      base.endsWith(".framework") ||
      base.endsWith(".dylib") ||
      base.endsWith(".so") ||
      base.endsWith(".xpc")
    ) {
      out.push(p);
      // Don't descend into already-collected nested bundles; codesign on the
      // outer bundle re-signs their internals as a unit if needed. (We still
      // collect deeper bundles via the walk below — the descent decision
      // happens before we add the entry.)
    }
    return st.isDirectory();
  });
  return out;
}

function walk(
  root: string,
  visit: (p: string) => boolean // return true to descend
): void {
  const shouldDescend = visit(root);
  if (!shouldDescend) return;
  let entries: string[];
  try {
    entries = readdirSync(root);
  } catch {
    return;
  }
  for (const entry of entries) {
    walk(path.join(root, entry), visit);
  }
}

function depth(p: string): number {
  return p.split(path.sep).length;
}

function isAppBundle(p: string): boolean {
  return p.endsWith(".app") && statSync(p).isDirectory();
}

function signOne(
  target: string,
  identity: string,
  entitlements: string | null
): void {
  const entFlag = entitlements ? `--entitlements "${entitlements}"` : "";
  const cmd =
    `codesign --force --timestamp --options runtime ` +
    `${entFlag} --sign "${identity}" "${target}"`;
  run(cmd, { silent: true });
}

function verifyCodesign(appBundle: string): void {
  log("Verifying code signature ...");
  run(`codesign --verify --strict --deep --verbose=2 "${appBundle}"`, {
    silent: false,
  });
  success("Signature verified");
}

// ---------------------------------------------------------------------------
// Notarization
// ---------------------------------------------------------------------------

async function notarize(artifactPath: string): Promise<void> {
  log(`Submitting ${path.basename(artifactPath)} to Apple notary service ...`);
  const args = buildNotarytoolAuthArgs();
  const code = await runStreaming("xcrun", [
    "notarytool",
    "submit",
    artifactPath,
    ...args,
    "--wait",
  ]);
  if (code !== 0) {
    error(
      `notarytool submit exited with code ${code}.\n` +
        "Run with --notarize again, or inspect the log via:\n" +
        "  xcrun notarytool log <submission-id> ..."
    );
    process.exit(1);
  }
  success("Notarization accepted");
}

function buildNotarytoolAuthArgs(): string[] {
  const profile = process.env.DAO_NOTARIZE_KEYCHAIN_PROFILE;
  if (profile) {
    return ["--keychain-profile", profile];
  }
  const appleId = process.env.DAO_NOTARIZE_APPLE_ID;
  const teamId = process.env.DAO_NOTARIZE_TEAM_ID;
  const password = process.env.DAO_NOTARIZE_PASSWORD;
  if (!appleId || !teamId || !password) {
    error(
      "Notarization credentials are missing.\n" +
        "Set either:\n" +
        "  DAO_NOTARIZE_KEYCHAIN_PROFILE=<profile-name>\n" +
        "(created via: xcrun notarytool store-credentials <profile> ...)\n" +
        "Or all three of:\n" +
        "  DAO_NOTARIZE_APPLE_ID, DAO_NOTARIZE_TEAM_ID, DAO_NOTARIZE_PASSWORD"
    );
    process.exit(1);
  }
  return [
    "--apple-id",
    appleId,
    "--team-id",
    teamId,
    "--password",
    password,
  ];
}

async function staple(artifactPath: string): Promise<void> {
  log(`Stapling notarization ticket onto ${path.basename(artifactPath)} ...`);
  const code = await runStreaming("xcrun", ["stapler", "staple", artifactPath]);
  if (code !== 0) {
    error(`stapler exited with code ${code}.`);
    process.exit(1);
  }
  success("Stapled");
}

function assessArtifact(artifactPath: string): void {
  log("Running spctl assessment ...");
  try {
    if (artifactPath.endsWith(".dmg")) {
      run(`spctl --assess --type install --verbose=2 "${artifactPath}"`, {
        silent: false,
      });
    } else if (artifactPath.endsWith(".zip")) {
      // .zip can't be assessed directly; skip with a note.
      warn(
        "spctl cannot assess .zip directly. Unpack and run: " +
          'spctl --assess --type execute -vv "Dao.app"'
      );
      return;
    } else {
      run(`spctl --assess --type execute --verbose=2 "${artifactPath}"`, {
        silent: false,
      });
    }
    success("Gatekeeper assessment passed");
  } catch {
    warn(
      "spctl assessment failed. Inspect manually:\n" +
        `  spctl --assess --type install -vv "${artifactPath}"`
    );
  }
}
