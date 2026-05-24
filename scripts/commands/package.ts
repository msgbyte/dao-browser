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
  openSync,
  readSync,
  closeSync,
  readFileSync,
  writeFileSync,
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

    // After a complete release pipeline (signed + notarized + stapled), the
    // remaining step is Sparkle EdDSA-signing the artifact and regenerating
    // appcast.xml. Surface that command so the operator doesn't forget.
    if (opts.signId && opts.notarize && opts.staple) {
      printAppcastNextStep(artifactPath);
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

interface RunStreamingCaptureResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

// Like runStreaming, but also captures stdout (still streamed live) so the
// caller can scan it (e.g. for notarytool's final "status: Accepted" line).
function runStreamingCapture(
  cmd: string,
  args: string[]
): Promise<RunStreamingCaptureResult> {
  console.log(chalk.dim(`$ ${cmd} ${args.map(shellEscape).join(" ")}`));
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout?.on("data", (chunk: Buffer) => {
      const s = chunk.toString();
      stdout += s;
      process.stdout.write(s);
    });
    child.stderr?.on("data", (chunk: Buffer) => {
      const s = chunk.toString();
      stderr += s;
      process.stderr.write(s);
    });
    child.on("error", (err: Error) =>
      resolve({ code: null, stdout, stderr: stderr + String(err) })
    );
    child.on("close", (code: number | null) =>
      resolve({ code, stdout, stderr })
    );
  });
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

  // codesign does not expand $(AppIdentifierPrefix) / $(CFBundleIdentifier)
  // — those are Xcode build-phase substitutions. Render the templates here
  // so the signed entitlements contain fully-qualified literals; an
  // unexpanded placeholder would make taskgated reject the signature with
  // "Code Signature Invalid" at launch.
  const teamId = extractTeamId(identity);
  const bundleId = readBundleIdentifier(appBundle);
  const renderDir = mkdtempSync(path.join(os.tmpdir(), "dao-entitlements-"));
  const mainRendered = renderEntitlements(
    ENTITLEMENTS,
    renderDir,
    "dao.entitlements",
    teamId,
    bundleId
  );
  const helperRendered = renderEntitlements(
    HELPER_ENTITLEMENTS,
    renderDir,
    "dao_helper.entitlements",
    teamId,
    bundleId
  );

  try {
    const items = collectSignables(appBundle);
    // Sort by depth descending so children sign before parents.
    items.sort((a, b) => depth(b) - depth(a));

    for (const item of items) {
      const ent = isAppBundle(item) ? helperRendered : null;
      signOne(item, identity, ent);
    }

    // Outer app last.
    signOne(appBundle, identity, mainRendered);
    success("App bundle signed");
  } finally {
    rmSync(renderDir, { recursive: true, force: true });
  }
}

// Pull the 10-char Team ID out of "Developer ID Application: Foo Bar (TEAMID1234)".
function extractTeamId(identity: string): string {
  const m = identity.match(/\(([A-Z0-9]{10})\)\s*$/);
  if (!m) {
    error(
      `Could not extract Team ID from DAO_SIGN_IDENTITY: "${identity}".\n` +
        '  Expected format: "Developer ID Application: <Name> (TEAMID1234)"'
    );
    process.exit(1);
  }
  return m[1];
}

function readBundleIdentifier(appBundle: string): string {
  const infoPlist = path.join(appBundle, "Contents", "Info.plist");
  if (!existsSync(infoPlist)) {
    error(`Info.plist not found inside app bundle: ${infoPlist}`);
    process.exit(1);
  }
  // `defaults read` strips the .plist suffix and prints the raw value.
  const out = run(
    `defaults read "${infoPlist.replace(/\.plist$/, "")}" CFBundleIdentifier`,
    { silent: true }
  ).trim();
  if (!out) {
    error(`CFBundleIdentifier is empty in ${infoPlist}`);
    process.exit(1);
  }
  return out;
}

// Render an entitlements template by substituting Xcode-style placeholders,
// then assert no `$(...)` remain. Any survivor would be silently signed into
// the binary and rejected at launch by taskgated.
//
// Also strips XML comments before writing: codesign's AMFI parser is stricter
// than plutil and rejects some perfectly valid UTF-8 inside <!-- ... -->
// (em dashes, backticks, etc.) with "AMFIUnserializeXML: syntax error".
// Comments are only useful for humans reading the source template anyway —
// no need to ship them into the signature.
function renderEntitlements(
  templatePath: string,
  outDir: string,
  outName: string,
  teamId: string,
  bundleId: string
): string {
  const src = readFileSync(templatePath, "utf8");
  const stripped = src.replace(/<!--[\s\S]*?-->/g, "");
  const rendered = stripped
    // $(AppIdentifierPrefix) expands to "<TeamID>." (trailing dot — that's
    // how Xcode's expansion works, since downstream values concatenate
    // "$(AppIdentifierPrefix)$(CFBundleIdentifier)" without a separator).
    .replace(/\$\(AppIdentifierPrefix\)/g, `${teamId}.`)
    .replace(/\$\(CFBundleIdentifier\)/g, bundleId);

  const leftover = rendered.match(/\$\([^)]+\)/);
  if (leftover) {
    error(
      `Unexpanded placeholder ${leftover[0]} in ${path.basename(templatePath)}.\n` +
        "  Add it to renderEntitlements() in scripts/commands/package.ts,\n" +
        "  or replace it with a literal value in the entitlements file."
    );
    process.exit(1);
  }

  const outPath = path.join(outDir, outName);
  writeFileSync(outPath, rendered);
  return outPath;
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
      return st.isDirectory();
    }

    // Bare executable Mach-O files inside framework Helpers/ subdirectories
    // (e.g. Chromium's app_mode_loader, chrome_crashpad_handler,
    // web_app_shortcut_copier, Sparkle's Autoupdate). codesign on the
    // outer framework does NOT recurse into these — Apple's notary will
    // reject the bundle because they're unsigned and lack hardened runtime.
    // Identify them by: regular file + has user-execute bit + Mach-O magic.
    if (st.isFile() && (st.mode & 0o100) !== 0 && isMachO(p)) {
      out.push(p);
      return false;
    }

    return st.isDirectory();
  });
  return out;
}

// Read the first 4 bytes and compare against Mach-O magic numbers.
function isMachO(p: string): boolean {
  try {
    const fd = openSync(p, "r");
    try {
      const buf = Buffer.alloc(4);
      const n = readSync(fd, buf, 0, 4, 0);
      if (n < 4) return false;
      const magic = buf.readUInt32BE(0);
      // 0xfeedface / 0xfeedfacf (32/64 BE), 0xcefaedfe / 0xcffaedfe (LE),
      // 0xcafebabe / 0xbebafeca (fat). Cover both endianness representations.
      return (
        magic === 0xfeedface ||
        magic === 0xfeedfacf ||
        magic === 0xcefaedfe ||
        magic === 0xcffaedfe ||
        magic === 0xcafebabe ||
        magic === 0xbebafeca
      );
    } finally {
      closeSync(fd);
    }
  } catch {
    return false;
  }
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
  // Last-line-of-defence: inspect the entitlements that actually got signed
  // into the bundle. Any `$(...)` survivor here means a placeholder slipped
  // past renderEntitlements, and taskgated will SIGKILL the app at launch
  // with "Code Signature Invalid".
  const embedded = run(
    `codesign -d --entitlements - --xml "${appBundle}"`,
    { silent: true }
  );
  const leftover = embedded.match(/\$\([^)]+\)/);
  if (leftover) {
    error(
      `Signed entitlements still contain unexpanded ${leftover[0]}.\n` +
        "  This will cause taskgated to reject the signature at launch.\n" +
        "  Fix: handle the placeholder in renderEntitlements()."
    );
    process.exit(1);
  }
  success("Signature verified");
}

// ---------------------------------------------------------------------------
// Notarization
// ---------------------------------------------------------------------------

async function notarize(artifactPath: string): Promise<void> {
  log(`Submitting ${path.basename(artifactPath)} to Apple notary service ...`);
  const args = buildNotarytoolAuthArgs();

  // Two-step notarization: short submit (no --wait), then poll info
  // ourselves with discrete short-lived xcrun calls. The bundled --wait
  // mode keeps a long-lived connection over which notarytool repeatedly
  // re-authenticates against the keychain, and somewhere mid-poll the
  // background process loses its keychain session — exiting 69 with a
  // misleading "No Keychain password item found" error even though the
  // submission was already accepted by Apple. Each poll below is a
  // brand-new short xcrun invocation, so the keychain session can't be
  // garbage-collected mid-call.
  const submitResult = await runStreamingCapture("xcrun", [
    "notarytool",
    "submit",
    artifactPath,
    ...args,
  ]);
  if (submitResult.code !== 0) {
    error(
      `notarytool submit exited with code ${submitResult.code}.\n` +
        "Inspect the most recent submission via:\n" +
        "  xcrun notarytool history --keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE"
    );
    process.exit(1);
  }
  const idMatch = submitResult.stdout.match(/id:\s*([0-9a-f-]{8,})/i);
  if (!idMatch) {
    error(
      "Could not extract submission id from notarytool output.\n" +
        "Inspect submissions manually via:\n" +
        "  xcrun notarytool history --keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE"
    );
    process.exit(1);
  }
  const submissionId = idMatch[1];
  log(`Submission accepted by Apple. id=${submissionId}`);
  log("Polling notarization status (Apple typically takes 2-10 minutes) ...");

  const finalStatus = await pollNotarizationStatus(submissionId, args);

  if (finalStatus.toLowerCase() !== "accepted") {
    error(
      `Notarization finished with status: ${finalStatus}.\n` +
        `Inspect the failure log via:\n` +
        `  xcrun notarytool log ${submissionId} --keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE`
    );
    process.exit(1);
  }
  success("Notarization accepted");
}

/**
 * Poll Apple notary service until the submission settles into a terminal
 * state. Each call is a discrete `xcrun notarytool info` invocation so
 * keychain access is short-lived and immune to session reaping.
 *
 * Strategy:
 *   - poll every 30s
 *   - timeout after 30 minutes (Apple usually finishes in 2-10 min;
 *     anything longer almost always means a stuck submission)
 *   - tolerate transient query failures (network blip, rate limit) by
 *     retrying up to 3 consecutive times before giving up
 */
async function pollNotarizationStatus(
  submissionId: string,
  authArgs: string[]
): Promise<string> {
  const POLL_INTERVAL_MS = 30_000;
  const TIMEOUT_MS = 30 * 60 * 1000;
  const MAX_TRANSIENT_FAILURES = 3;

  const startedAt = Date.now();
  let consecutiveFailures = 0;
  let pollCount = 0;

  while (Date.now() - startedAt < TIMEOUT_MS) {
    pollCount += 1;
    const elapsedSec = Math.floor((Date.now() - startedAt) / 1000);
    const result = await runQuiet("xcrun", [
      "notarytool",
      "info",
      submissionId,
      ...authArgs,
    ]);

    if (result.code !== 0) {
      consecutiveFailures += 1;
      warn(
        `Status poll #${pollCount} failed (exit ${result.code}, ` +
          `${consecutiveFailures}/${MAX_TRANSIENT_FAILURES} transient): ` +
          (result.stderr.trim().split("\n").pop() || "<no stderr>")
      );
      if (consecutiveFailures >= MAX_TRANSIENT_FAILURES) {
        error(
          `Giving up after ${consecutiveFailures} consecutive query failures.\n` +
            `Submission id: ${submissionId}\n` +
            "Check status manually:\n" +
            `  xcrun notarytool info ${submissionId} ` +
            "--keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE"
        );
        process.exit(1);
      }
      await sleep(POLL_INTERVAL_MS);
      continue;
    }
    consecutiveFailures = 0;

    const statusMatch = result.stdout.match(/status:\s*([\w ]+)\s*$/im);
    const status = (statusMatch ? statusMatch[1] : "").trim();
    console.log(
      chalk.dim(
        `  [${formatElapsed(elapsedSec)}] poll #${pollCount}: status=${status || "<unknown>"}`
      )
    );

    // Terminal states per Apple docs: Accepted, Invalid, Rejected.
    // "In Progress" and unknown values mean keep polling.
    const lower = status.toLowerCase();
    if (lower === "accepted" || lower === "invalid" || lower === "rejected") {
      return status;
    }
    await sleep(POLL_INTERVAL_MS);
  }

  error(
    `Notarization timed out after ${Math.floor(TIMEOUT_MS / 60_000)} minutes.\n` +
      `Submission id: ${submissionId}\n` +
      "It may still finish later. Check status manually:\n" +
      `  xcrun notarytool info ${submissionId} ` +
      "--keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE"
  );
  process.exit(1);
}

interface QuietResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

// Like runStreamingCapture but suppresses live output — for status polls
// where we only care about the final stdout, not tailing it on screen.
function runQuiet(cmd: string, args: string[]): Promise<QuietResult> {
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout?.on("data", (chunk: Buffer) => (stdout += chunk.toString()));
    child.stderr?.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    child.on("error", (err: Error) =>
      resolve({ code: null, stdout, stderr: stderr + String(err) })
    );
    child.on("close", (code: number | null) =>
      resolve({ code, stdout, stderr })
    );
  });
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function formatElapsed(sec: number): string {
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
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

// ---------------------------------------------------------------------------
// Post-release guidance
// ---------------------------------------------------------------------------

/**
 * Print a clear next-step prompt after package:release finishes.
 * The release pipeline ends with a stapled .dmg/.zip in dist/, but Sparkle
 * still needs to EdDSA-sign the artifact and (re)generate appcast.xml. That
 * step is intentionally manual (it touches the keychain private key), so we
 * just remind the operator to run it.
 */
function printAppcastNextStep(artifactPath: string): void {
  const generateAppcast = path.join(
    "third_party",
    "sparkle",
    "bin",
    "generate_appcast"
  );
  const generateAppcastAbs = path.join(ROOT_DIR, generateAppcast);
  const appcastTemplate = path.join(ROOT_DIR, "branding", "appcast.template.xml");
  const appcastDest = path.join(DIST_DIR, "appcast.xml");
  const artifactRel = path.relative(ROOT_DIR, artifactPath) || artifactPath;

  console.log("");
  log("Release artifact ready: " + chalk.bold(artifactRel));
  console.log("");
  log(chalk.bold("Next step — Sparkle-sign and (re)generate appcast.xml:"));

  if (!existsSync(generateAppcastAbs)) {
    warn(
      "generate_appcast not found at " +
        generateAppcast +
        ".\n  Run `npm run sparkle:fetch` first to install Sparkle's tools."
    );
  }

  // First-release bootstrap: appcast.xml has to exist before generate_appcast
  // can append to it. Mention the seed only when dist/appcast.xml is missing.
  if (!existsSync(appcastDest) && existsSync(appcastTemplate)) {
    console.log(
      chalk.dim("  # First release? Seed appcast.xml from the template:")
    );
    console.log(
      "  cp branding/appcast.template.xml dist/appcast.xml"
    );
  }
  console.log("  " + chalk.cyan(`${generateAppcast} dist/`));
  console.log("");
  console.log(
    chalk.dim(
      "  Then publish dist/appcast.xml together with the artifact in your GitHub Release."
    )
  );
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
