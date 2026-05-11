import { Command } from "commander";
import { copyFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
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
    // Step 1 — bump version in dao.json
    // ------------------------------------------------------------------
    const config = loadConfig();
    const oldVersion = config.version.display;
    const newVersion = bumpVersion(oldVersion, opts.bump || "patch");
    log(`Bumping version: ${oldVersion} → ${newVersion}`);
    if (!opts.dryRun) {
      writeDaoVersion(newVersion);
    }

    const arch = config.build.target_cpu;
    const baseName = `dao-browser-${newVersion}-mac-${arch}`;
    const dmgName = `${baseName}.dmg`;
    const dmgPath = path.join(ROOT_DIR, "dist", dmgName);

    // Pre-flight: appcast seed must already exist in dist/.
    const appcastDest = path.join(ROOT_DIR, "dist", "appcast.xml");
    if (!opts.skipBuild && !existsSync(appcastDest)) {
      error(
        "dist/appcast.xml does not exist.\n" +
          "Seed it once (first release) via:\n" +
          "  cp branding/appcast.template.xml dist/appcast.xml\n" +
          "Then rerun the release."
      );
      process.exit(1);
    }

    // ------------------------------------------------------------------
    // Step 2 — import:force (re-applies patches and copies src/dao/ over)
    // ------------------------------------------------------------------
    if (!opts.skipBuild) {
      await runStep(
        opts.dryRun,
        "Importing patches (force)",
        "npm",
        ["run", "import:force"]
      );

      // ----------------------------------------------------------------
      // Step 3 — build (release, since this artifact ships to users)
      // Note: package:release internally rebuilds via gn+ninja only if
      // the inputs changed, but the project's convention is to do a
      // dedicated build step first to surface compile errors early.
      // ----------------------------------------------------------------
      await runStep(opts.dryRun, "Building (release)", "npm", [
        "run",
        "build",
      ]);

      // ----------------------------------------------------------------
      // Step 4 — package:release (sign + notarize + staple)
      // ----------------------------------------------------------------
      await runStep(
        opts.dryRun,
        "Packaging (sign + notarize + staple)",
        "npm",
        ["run", "package:release"]
      );
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
      const uploadArgs = ["run", "upload:release", "--", dmgPath];
      if (opts.bucket) uploadArgs.push("--bucket", opts.bucket);
      if (opts.prefix) uploadArgs.push("--prefix", opts.prefix);
      await runStep(
        opts.dryRun,
        `Uploading ${dmgName} to R2`,
        "npm",
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
