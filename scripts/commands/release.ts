import { Command } from "commander";
import { spawn } from "node:child_process";
import { copyFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import chalk from "chalk";
import {
  ROOT_DIR,
  loadConfig,
  log,
  success,
  warn,
  error,
  runStreaming,
} from "../utils.js";

interface ReleaseOptions {
  bump?: "patch" | "minor" | "major";
  bucket?: string;
  prefix?: string;
  skipUpload?: boolean;
  skipBuild?: boolean;
  skipBump?: boolean;
  // Resume from staple: dmg has already been notarized externally
  // (e.g. you ran `xcrun stapler staple dist/...dmg` yourself), and
  // we just need to finish appcast + copy + upload.
  resumeFromStaple?: boolean;
  dryRun?: boolean;
}

const REQUIRED_UPLOAD_ENV = [
  "CLOUDFLARE_ACCOUNT_ID",
  "CLOUDFLARE_API_TOKEN",
] as const;

// Required for `package:release` (signs with Developer ID + notarizes).
// Notarization accepts EITHER a stored keychain profile OR a triple of
// apple-id/team-id/password — we check for at least one valid combination.
const SIGN_IDENTITY_ENV = "DAO_SIGN_IDENTITY";
const NOTARIZE_PROFILE_ENV = "DAO_NOTARIZE_KEYCHAIN_PROFILE";
const NOTARIZE_TRIPLE_ENV = [
  "DAO_NOTARIZE_APPLE_ID",
  "DAO_NOTARIZE_TEAM_ID",
  "DAO_NOTARIZE_PASSWORD",
] as const;

export const releaseCommand = new Command("release")
  .description(
    "End-to-end release: bump version, import, build, package:release, " +
      "generate appcast, copy appcast to website/public, upload .dmg to R2."
  )
  .option(
    "--bump <type>",
    "Version bump kind: patch | minor | major",
    (v: string): "patch" | "minor" | "major" => {
      if (v !== "patch" && v !== "minor" && v !== "major") {
        throw new Error(`Invalid --bump value: ${v}`);
      }
      return v;
    },
    "patch" as const
  )
  .option("-b, --bucket <name>", "R2 bucket (or $R2_BUCKET)")
  .option("-p, --prefix <prefix>", "R2 key prefix for the .dmg", "")
  .option("--skip-upload", "Skip the R2 upload step (still produces artifacts)")
  .option("--skip-build", "Skip import + build (use existing dist/ artifact)")
  .option(
    "--skip-bump",
    "Use the version already in dao.json instead of bumping " +
      "(use when resuming a release that failed mid-way)"
  )
  .option(
    "--resume-from-staple",
    "Skip build + sign + notarize. Use this when you've already " +
      "notarized + stapled the dmg manually after a notarytool keychain " +
      "failure. Continues from generate_appcast onward."
  )
  .option("--dry-run", "Print steps without executing them")
  .action(async (opts: ReleaseOptions) => {
    // ------------------------------------------------------------------
    // Pre-flight: fail fast on every missing piece BEFORE the hour-long
    // build, so the operator doesn't discover a broken env at packaging
    // time. Collect all problems first and report together — fixing one
    // env var, restarting, then hitting the next one is the worst UX.
    // ------------------------------------------------------------------
    const willUpload = !opts.skipUpload;
    const willBuild = !opts.skipBuild;
    const bucket = opts.bucket || process.env.R2_BUCKET;
    const problems: string[] = [];

    if (willBuild) {
      // package:release requires a Developer ID identity for codesign.
      if (!process.env[SIGN_IDENTITY_ENV]) {
        problems.push(
          `Missing ${SIGN_IDENTITY_ENV} (required by package:release).\n` +
            '  Example: export DAO_SIGN_IDENTITY="Developer ID Application: Foo Bar (TEAMID1234)"\n' +
            "  List installed identities: security find-identity -v -p codesigning"
        );
      }
      // Notarization auth: either a keychain profile, or all three of
      // apple-id/team-id/password. Anything in between is a misconfig.
      const hasProfile = !!process.env[NOTARIZE_PROFILE_ENV];
      const tripleSet = NOTARIZE_TRIPLE_ENV.filter((k) => !!process.env[k]);
      if (!hasProfile && tripleSet.length === 0) {
        problems.push(
          "Missing notarization credentials (required by package:release).\n" +
            "  Set EITHER:\n" +
            `    ${NOTARIZE_PROFILE_ENV}=<profile-name>\n` +
            "    (created via: xcrun notarytool store-credentials <profile> ...)\n" +
            "  OR all three of:\n" +
            `    ${NOTARIZE_TRIPLE_ENV.join(", ")}`
        );
      } else if (
        !hasProfile &&
        tripleSet.length > 0 &&
        tripleSet.length < NOTARIZE_TRIPLE_ENV.length
      ) {
        const missingTriple = NOTARIZE_TRIPLE_ENV.filter(
          (k) => !process.env[k]
        );
        problems.push(
          `Incomplete notarization credentials: missing ${missingTriple.join(
            ", "
          )}.\n` +
            "  Either set all three apple-id/team-id/password vars, or use " +
            `${NOTARIZE_PROFILE_ENV} instead.`
        );
      }
      // Sparkle EdDSA signing key — generate_appcast reads this from the
      // login keychain, but we can at least verify the binary exists now.
      const generateAppcast = path.join(
        ROOT_DIR,
        "third_party",
        "sparkle",
        "bin",
        "generate_appcast"
      );
      if (!existsSync(generateAppcast)) {
        problems.push(
          "third_party/sparkle/bin/generate_appcast not found.\n" +
            "  Run `npm run sparkle:fetch` first."
        );
      }
    }

    if (willUpload) {
      const missing = REQUIRED_UPLOAD_ENV.filter((k) => !process.env[k]);
      if (missing.length > 0) {
        problems.push(
          `Missing env var(s) required for R2 upload: ${missing.join(", ")}.\n` +
            "  Set them, or pass --skip-upload to produce artifacts only."
        );
      }
      if (!bucket) {
        problems.push(
          "R2 bucket not specified.\n" +
            "  Pass --bucket or set R2_BUCKET, or use --skip-upload."
        );
      }
    }

    if (problems.length > 0) {
      error(
        `Pre-flight check failed (${problems.length} issue(s)):\n\n` +
          problems.map((p, i) => `[${i + 1}] ${p}`).join("\n\n")
      );
      process.exit(1);
    }
    success("Pre-flight check passed");

    // ------------------------------------------------------------------
    // Step 1 — bump version in dao.json (or reuse the current one when
    // resuming a release that failed mid-way, so dao.json doesn't get
    // double-bumped on retry).
    // ------------------------------------------------------------------
    const config = loadConfig();
    const oldVersion = config.version.display;
    let newVersion: string;
    if (opts.skipBump) {
      newVersion = oldVersion;
      log(`Reusing version from dao.json: ${newVersion} (--skip-bump)`);
    } else {
      newVersion = bumpVersion(oldVersion, opts.bump || "patch");
      log(`Bumping version: ${oldVersion} → ${newVersion}`);
      if (!opts.dryRun) {
        writeDaoVersion(newVersion);
      }
    }

    const arch = config.build.target_cpu;
    const baseName = `dao-browser-${newVersion}-mac-${arch}`;
    const dmgName = `${baseName}.dmg`;
    const dmgPath = path.join(ROOT_DIR, "dist", dmgName);

    // Seed dist/appcast.xml from the authoritative source before
    // generate_appcast appends to it. dist/ is treated as ephemeral
    // (may be wiped between runs), so we always re-seed from
    // website/public/appcast.xml — that's the canonical history of
    // shipped releases. Falls back to branding/appcast.template.xml
    // only on the very first release when neither exists yet.
    const appcastDest = path.join(ROOT_DIR, "dist", "appcast.xml");
    const appcastPublic = path.join(
      ROOT_DIR,
      "website",
      "public",
      "appcast.xml"
    );
    const appcastTemplate = path.join(
      ROOT_DIR,
      "branding",
      "appcast.template.xml"
    );
    if (existsSync(appcastPublic)) {
      log(
        `Seeding dist/appcast.xml from ${path.relative(
          ROOT_DIR,
          appcastPublic
        )}`
      );
      if (!opts.dryRun) {
        copyFileSync(appcastPublic, appcastDest);
      }
    } else if (existsSync(appcastTemplate)) {
      warn(
        `${path.relative(ROOT_DIR, appcastPublic)} not found — ` +
          "seeding from template (first release?)."
      );
      if (!opts.dryRun) {
        copyFileSync(appcastTemplate, appcastDest);
      }
    } else {
      error(
        "Cannot seed dist/appcast.xml — neither\n" +
          `  ${path.relative(ROOT_DIR, appcastPublic)}\n` +
          "nor\n" +
          `  ${path.relative(ROOT_DIR, appcastTemplate)}\n` +
          "exists. Restore one of them and rerun."
      );
      process.exit(1);
    }

    // ------------------------------------------------------------------
    // Steps 2-4 — import + build + package (sign + notarize + staple)
    //
    // We intentionally DON'T pass --notarize/--staple to the package
    // subcommand. notarize is run by release itself below so that on
    // failure we can stop cleanly and print recovery instructions for
    // the operator, rather than getting trapped inside the package
    // subcommand's exit path. (Background: macOS occasionally rejects
    // `xcrun notarytool submit` from a long-running process with the
    // misleading error "No Keychain password item found". The recovery
    // is to run that one command from your interactive shell — see
    // printNotarizeRecoveryGuide() for the exact steps.)
    // ------------------------------------------------------------------
    const skipBuildPhase = opts.skipBuild || opts.resumeFromStaple;
    if (!skipBuildPhase) {
      await runStep(
        opts.dryRun,
        "Importing patches (force)",
        "npx",
        ["tsx", "scripts/cli.ts", "import", "--force"]
      );

      await runStep(opts.dryRun, "Building (release)", "npx", [
        "tsx",
        "scripts/cli.ts",
        "build",
      ]);

      // Package WITHOUT notarize/staple — release handles those itself
      // so we can intercept notarize failures and print a recovery guide.
      await runStep(
        opts.dryRun,
        "Packaging (sign only — notarize handled separately)",
        "npx",
        ["tsx", "scripts/cli.ts", "package", "--sign-id"]
      );

      // ----------------------------------------------------------------
      // Step 4b — notarize the .dmg (release-controlled so we can give
      // a useful recovery path if it fails)
      // ----------------------------------------------------------------
      if (!opts.dryRun) {
        await notarizeOrGuide(dmgPath, dmgName);
        await stapleOrGuide(dmgPath, dmgName);
      } else {
        log("Submitting to Apple notary service");
        console.log(
          `  [dry-run] xcrun notarytool submit ${dmgPath} ` +
            `--keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE --wait`
        );
        log("Stapling notarization ticket");
        console.log(`  [dry-run] xcrun stapler staple ${dmgPath}`);
      }
    } else if (opts.resumeFromStaple) {
      // Operator already notarized + stapled the dmg manually after a
      // failed run. Verify they actually did the staple before we keep
      // going — otherwise generate_appcast emits a feed that points at
      // an unnotarized dmg and Gatekeeper will block users.
      log("Resuming from staple (verifying dmg has notarization ticket)");
      if (!opts.dryRun) {
        await assertStapled(dmgPath);
      }
    } else {
      warn("Skipping build/package (--skip-build)");
    }

    // Sanity-check that the .dmg we expect actually exists before generating
    // the appcast (otherwise generate_appcast walks an empty dist/ and we
    // ship a feed without our new release).
    if (!opts.dryRun && !existsSync(dmgPath)) {
      error(
        `Expected artifact not found: ${path.relative(ROOT_DIR, dmgPath)}\n` +
          "package:release should have produced this. Inspect dist/ and rerun."
      );
      process.exit(1);
    }

    // ------------------------------------------------------------------
    // Step 5 — generate_appcast dist/  (Sparkle EdDSA-signs each enclosure)
    // ------------------------------------------------------------------
    const generateAppcast = path.join(
      ROOT_DIR,
      "third_party",
      "sparkle",
      "bin",
      "generate_appcast"
    );
    await runStep(
      opts.dryRun,
      "Generating Sparkle appcast",
      generateAppcast,
      [path.join(ROOT_DIR, "dist")]
    );

    // ------------------------------------------------------------------
    // Step 5b — copy regenerated appcast to website/public/ so the
    // public feed (https://dao.msgbyte.com/appcast.xml) reflects the
    // new release the next time the website deploys.
    // ------------------------------------------------------------------
    const websiteAppcast = path.join(
      ROOT_DIR,
      "website",
      "public",
      "appcast.xml"
    );
    log(
      `Copying appcast → ${path.relative(ROOT_DIR, websiteAppcast)}`
    );
    if (!opts.dryRun) {
      copyFileSync(appcastDest, websiteAppcast);
    }

    // ------------------------------------------------------------------
    // Step 5c — update website/public/info.json so the /download route
    // points at the new artifact. Pulled in here (not as a manual step)
    // because forgetting it leaves the website handing out the old .dmg.
    // ------------------------------------------------------------------
    const infoJsonPath = path.join(
      ROOT_DIR,
      "website",
      "public",
      "info.json"
    );
    log(`Updating ${path.relative(ROOT_DIR, infoJsonPath)}`);
    if (!opts.dryRun) {
      updateInfoJson(infoJsonPath, {
        version: newVersion,
        chromiumVersion: config.version.version,
        releasedAt: today(),
      });
    }

    // ------------------------------------------------------------------
    // Step 6 — upload .dmg to R2
    // ------------------------------------------------------------------
    if (willUpload) {
      const uploadArgs = ["tsx", "scripts/cli.ts", "upload", dmgPath];
      if (opts.bucket) uploadArgs.push("--bucket", opts.bucket);
      if (opts.prefix) uploadArgs.push("--prefix", opts.prefix);
      await runStep(
        opts.dryRun,
        `Uploading ${dmgName} to R2`,
        "npx",
        uploadArgs
      );
    } else {
      warn("Skipping R2 upload (--skip-upload)");
    }

    success(`Release ${newVersion} ready.`);
    log("Next manual steps (not done by this script):");
    log(
      "  - Commit dao.json + website/public/appcast.xml + " +
        "website/public/info.json, tag v" +
        newVersion +
        ", push."
    );
    log("  - Deploy the website so dao.msgbyte.com/appcast.xml updates.");
    log(
      "  - (Optional) Create a GitHub Release with the .dmg from dist/."
    );
  });

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function runStep(
  dryRun: boolean | undefined,
  description: string,
  cmd: string,
  args: string[]
): Promise<void> {
  log(description);
  if (dryRun) {
    console.log(`  [dry-run] ${cmd} ${args.join(" ")}`);
    return;
  }
  const code = await runStreaming(cmd, args);
  if (code !== 0) {
    error(`Step failed (${description}): ${cmd} exited with code ${code}`);
    process.exit(1);
  }
}

function bumpVersion(
  current: string,
  kind: "patch" | "minor" | "major"
): string {
  const m = current.match(/^(\d+)\.(\d+)\.(\d+)(.*)$/);
  if (!m) {
    error(
      `Cannot parse version "${current}" — expected MAJOR.MINOR.PATCH form.`
    );
    process.exit(1);
  }
  let major = parseInt(m[1], 10);
  let minor = parseInt(m[2], 10);
  let patch = parseInt(m[3], 10);
  if (kind === "major") {
    major += 1;
    minor = 0;
    patch = 0;
  } else if (kind === "minor") {
    minor += 1;
    patch = 0;
  } else {
    patch += 1;
  }
  return `${major}.${minor}.${patch}`;
}

function writeDaoVersion(newVersion: string): void {
  const configPath = path.join(ROOT_DIR, "dao.json");
  const raw = readFileSync(configPath, "utf-8");
  // Preserve formatting by doing a targeted regex replacement on the
  // version.display field rather than reserializing JSON.
  const updated = raw.replace(
    /("display"\s*:\s*")[^"]+(")/,
    `$1${newVersion}$2`
  );
  if (updated === raw) {
    error("Failed to update version.display in dao.json");
    process.exit(1);
  }
  writeFileSync(configPath, updated);
  success(`dao.json version.display → ${newVersion}`);
}

interface InfoJsonUpdate {
  version: string;
  chromiumVersion: string;
  releasedAt: string;
}

// Mutate website/public/info.json in place: bump version + releasedAt + the
// version segment inside each platform URL. The host stays the same (it's
// a separate concern — switching CDN host shouldn't be coupled to a version
// bump). Done as targeted string substitutions so we preserve key ordering,
// the leading $schema description, and any future fields we don't recognize.
function updateInfoJson(filePath: string, update: InfoJsonUpdate): void {
  if (!existsSync(filePath)) {
    error(`info.json not found: ${filePath}`);
    process.exit(1);
  }
  const raw = readFileSync(filePath, "utf-8");
  const parsed = JSON.parse(raw) as {
    version: string;
    chromiumVersion?: string;
    releasedAt?: string;
    platforms?: Record<string, { url?: string; label?: string }>;
  };
  const oldVersion = parsed.version;
  if (!oldVersion) {
    error("info.json has no `version` field");
    process.exit(1);
  }

  let next = raw;
  next = replaceField(next, "version", update.version);
  next = replaceField(next, "chromiumVersion", update.chromiumVersion);
  next = replaceField(next, "releasedAt", update.releasedAt);

  // Replace the old version inside platform URLs only — don't blanket-replace,
  // since the same number could legitimately appear elsewhere later.
  if (parsed.platforms) {
    for (const platform of Object.values(parsed.platforms)) {
      const url = platform?.url;
      if (!url || !url.includes(oldVersion)) continue;
      const updatedUrl = url.split(oldVersion).join(update.version);
      next = next.replace(jsonStringLiteral(url), jsonStringLiteral(updatedUrl));
    }
  }

  if (next === raw) {
    warn("info.json unchanged (already up-to-date?)");
    return;
  }
  writeFileSync(filePath, next);
  success(
    `info.json → version ${update.version}, releasedAt ${update.releasedAt}`
  );
}

function replaceField(raw: string, key: string, value: string): string {
  const re = new RegExp(`("${key}"\\s*:\\s*")[^"]*(")`);
  return raw.replace(re, `$1${value}$2`);
}

// Render a string the way it would appear inside JSON (with surrounding quotes
// and \-escaping) so the URL replacement matches the exact source bytes.
function jsonStringLiteral(s: string): string {
  return JSON.stringify(s);
}

function today(): string {
  const d = new Date();
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, "0");
  const dd = String(d.getDate()).padStart(2, "0");
  return `${yyyy}-${mm}-${dd}`;
}

// ---------------------------------------------------------------------------
// Notarize + staple, with a useful recovery path on keychain failures
// ---------------------------------------------------------------------------

interface SpawnInheritResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

// Run a command, streaming stdout+stderr live to the user AND capturing them
// so we can scan for known failure patterns (e.g. notarytool's keychain bug).
function spawnInheritCapture(
  cmd: string,
  args: string[]
): Promise<SpawnInheritResult> {
  console.log(chalk.dim(`$ ${cmd} ${args.join(" ")}`));
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
    child.on("error", (err) =>
      resolve({ code: null, stdout, stderr: stderr + String(err) })
    );
    child.on("close", (code) => resolve({ code, stdout, stderr }));
  });
}

async function notarizeOrGuide(
  dmgPath: string,
  dmgName: string
): Promise<void> {
  log(`Submitting ${dmgName} to Apple notary service ...`);
  const profile = process.env.DAO_NOTARIZE_KEYCHAIN_PROFILE;
  const authArgs = profile
    ? ["--keychain-profile", profile]
    : [
        "--apple-id",
        process.env.DAO_NOTARIZE_APPLE_ID || "",
        "--team-id",
        process.env.DAO_NOTARIZE_TEAM_ID || "",
        "--password",
        process.env.DAO_NOTARIZE_PASSWORD || "",
      ];

  const result = await spawnInheritCapture("xcrun", [
    "notarytool",
    "submit",
    dmgPath,
    ...authArgs,
    "--wait",
  ]);

  // notarytool exits 0 on success AND on Rejected/Invalid; only the
  // final "status:" line tells the truth.
  if (result.code === 0) {
    const finalStatus = (
      result.stdout.match(/status:\s*(\w+)\s*$/im) || []
    )[1];
    if (finalStatus && finalStatus.toLowerCase() === "accepted") {
      success("Notarization accepted");
      return;
    }
    const submissionId = (
      result.stdout.match(/id:\s*([0-9a-f-]{8,})/i) || []
    )[1];
    error(
      `Notarization finished with status: ${finalStatus || "<unknown>"}.\n` +
        (submissionId
          ? `Inspect the failure log via:\n  xcrun notarytool log ${submissionId} --keychain-profile ${profile || "<your-profile>"}`
          : "")
    );
    process.exit(1);
  }

  // Non-zero exit. The failure most operators hit here is the macOS
  // keychain bug: "No Keychain password item found for profile: ..."
  // even though that profile works fine when you call notarytool from
  // your interactive shell. Print the full recovery guide.
  printNotarizeRecoveryGuide(dmgPath, dmgName, result.stderr);
  process.exit(1);
}

async function stapleOrGuide(
  dmgPath: string,
  dmgName: string
): Promise<void> {
  log(`Stapling notarization ticket onto ${dmgName} ...`);
  const result = await spawnInheritCapture("xcrun", [
    "stapler",
    "staple",
    dmgPath,
  ]);
  if (result.code === 0) {
    success("Stapled");
    return;
  }
  error(
    `stapler exited with code ${result.code}.\n` +
      "If notarization succeeded but staple failed, run manually then resume:\n" +
      `  xcrun stapler staple ${path.relative(ROOT_DIR, dmgPath)}\n` +
      "  npm run release -- --skip-bump --resume-from-staple"
  );
  process.exit(1);
}

// Verify the dmg has a stapled notarization ticket. Used by --resume-from-staple
// to fail fast if the operator forgot to actually run `xcrun stapler staple`
// before resuming.
async function assertStapled(dmgPath: string): Promise<void> {
  const result = await spawnInheritCapture("xcrun", [
    "stapler",
    "validate",
    dmgPath,
  ]);
  if (result.code === 0) {
    success("dmg has a valid notarization ticket");
    return;
  }
  error(
    "--resume-from-staple was passed but the dmg is NOT stapled.\n" +
      "Before resuming, you need to:\n" +
      "  1. Notarize the dmg (see recovery guide below)\n" +
      "  2. Staple it: xcrun stapler staple " +
      path.relative(ROOT_DIR, dmgPath) +
      "\n" +
      "  3. Then rerun: npm run release -- --skip-bump --resume-from-staple\n\n" +
      "Recovery guide:"
  );
  printNotarizeRecoveryGuide(
    dmgPath,
    path.basename(dmgPath),
    /*stderrFromFailedRun*/ ""
  );
  process.exit(1);
}

// Print exactly what to do when notarytool fails with the keychain error.
// This is THE escape hatch — every step here can be copy-pasted into the
// operator's interactive shell, where notarytool consistently works.
function printNotarizeRecoveryGuide(
  dmgPath: string,
  dmgName: string,
  stderr: string
): void {
  const profile = process.env.DAO_NOTARIZE_KEYCHAIN_PROFILE || "<your-profile>";
  const dmgRel = path.relative(ROOT_DIR, dmgPath);
  const isKnownKeychainBug = /No Keychain password item found/i.test(stderr);

  console.log("");
  console.log(chalk.yellow("━".repeat(72)));
  console.log(chalk.yellow.bold("  Notarization step failed."));
  if (isKnownKeychainBug) {
    console.log(
      chalk.yellow(
        "  This is the known macOS keychain bug — notarytool can't see\n" +
          "  the keychain profile from a long-running script process,\n" +
          "  even though it works fine from your interactive shell."
      )
    );
  }
  console.log(chalk.yellow("━".repeat(72)));
  console.log("");
  console.log(chalk.bold("Recovery — run this single command in your terminal:"));
  console.log("");
  console.log(
    `  xcrun notarytool submit ${dmgRel} --keychain-profile ${profile} --wait \\\n` +
      `    && xcrun stapler staple ${dmgRel} \\\n` +
      `    && npm run release -- --skip-bump --resume-from-staple`
  );
  console.log("");
  console.log(
    chalk.dim(
      "  # If notarytool submit ALSO fails in your shell, the keychain profile is\n" +
        "  # genuinely broken — recreate it with:\n" +
        `  #   xcrun notarytool store-credentials ${profile} \\\n` +
        "  #     --apple-id <you@example.com> --team-id <TEAMID> --password <app-specific-pw>"
    )
  );
  console.log("");
  console.log(chalk.yellow("━".repeat(72)));
  console.log("");
  log("dao.json is already at " + dmgName.match(/\d+\.\d+\.\d+/)?.[0] + ".");
  log(
    "After step 3 finishes, you'll have appcast.xml regenerated, " +
      "info.json updated, and the dmg uploaded to R2."
  );
}

