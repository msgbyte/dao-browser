import { Command } from "commander";
import {
  createHash,
} from "node:crypto";
import {
  createReadStream,
  existsSync,
  mkdirSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { pipeline } from "node:stream/promises";
import path from "node:path";
import {
  ROOT_DIR,
  THIRD_PARTY_DIR,
  loadConfig,
  log,
  success,
  warn,
  error,
  run,
  runStreaming,
} from "../utils.js";

const SPARKLE_DIR = path.join(THIRD_PARTY_DIR, "sparkle");
const FRAMEWORK_PATH = path.join(SPARKLE_DIR, "Sparkle.framework");
const VERSION_STAMP = path.join(SPARKLE_DIR, ".version");
const CACHE_DIR = path.join(SPARKLE_DIR, ".cache");

export const sparkleCommand = new Command("sparkle")
  .description("Manage the Sparkle auto-update framework")
  .addCommand(makeFetchCommand())
  .addCommand(makeKeygenCommand())
  .addCommand(makeSignCommand());

function makeFetchCommand(): Command {
  return new Command("fetch")
    .description("Download and unpack Sparkle.framework into third_party/sparkle/")
    .option("--force", "Re-download even if a matching version is already present")
    .action(async (opts: { force?: boolean }) => {
      const config = loadConfig();
      if (!config.sparkle) {
        error("dao.json has no `sparkle` section.");
        process.exit(1);
      }
      const { version, url, sha256 } = config.sparkle;

      if (
        !opts.force &&
        existsSync(FRAMEWORK_PATH) &&
        readVersionStamp() === version
      ) {
        success(`Sparkle ${version} already present at third_party/sparkle/`);
        return;
      }

      mkdirSync(SPARKLE_DIR, { recursive: true });
      mkdirSync(CACHE_DIR, { recursive: true });

      const archiveName = path.basename(url);
      const archivePath = path.join(CACHE_DIR, archiveName);

      if (!existsSync(archivePath)) {
        log(`Downloading Sparkle ${version} from ${url}`);
        await downloadFile(url, archivePath);
      } else {
        log(`Using cached archive ${archiveName}`);
      }

      log("Verifying archive sha256 ...");
      const actual = await sha256File(archivePath);
      if (sha256 === "" || /^0+$/.test(sha256)) {
        warn(
          "dao.json sparkle.sha256 is empty. Computed sha256 of the downloaded archive:\n" +
            `  ${actual}\n` +
            "Update dao.json with this value (after cross-checking the published checksum) and rerun.\n" +
            "Refusing to install an unverified Sparkle build."
        );
        process.exit(1);
      }
      // Detect placeholder values (all-same-character) and demand a real one.
      if (looksLikePlaceholder(sha256)) {
        warn(
          "dao.json sparkle.sha256 looks like a placeholder. Computed sha256:\n" +
            `  ${actual}\n` +
            "Cross-check this against https://github.com/sparkle-project/Sparkle/releases/tag/" +
            version +
            " and replace the placeholder in dao.json. Refusing to continue."
        );
        process.exit(1);
      }
      if (actual.toLowerCase() !== sha256.toLowerCase()) {
        error(
          "Sparkle archive sha256 mismatch.\n" +
            `  expected: ${sha256}\n` +
            `  actual:   ${actual}\n` +
            "If you intentionally upgraded sparkle.version, update sparkle.sha256 too."
        );
        process.exit(1);
      }
      success("sha256 OK");

      // Clean previous extraction.
      if (existsSync(FRAMEWORK_PATH)) {
        rmSync(FRAMEWORK_PATH, { recursive: true, force: true });
      }

      log("Extracting Sparkle.framework ...");
      // The Sparkle release tarball contains Sparkle.framework + bin tools at
      // the root. We extract directly into SPARKLE_DIR.
      run(`tar -xf "${archivePath}" -C "${SPARKLE_DIR}"`, { silent: true });

      if (!existsSync(FRAMEWORK_PATH)) {
        error(
          `Extraction did not produce ${FRAMEWORK_PATH}.\n` +
            "Inspect the archive layout manually:\n" +
            `  tar -tf "${archivePath}" | head`
        );
        process.exit(1);
      }

      writeVersionStamp(version);
      success(`Sparkle ${version} ready at third_party/sparkle/Sparkle.framework`);
    });
}

function makeKeygenCommand(): Command {
  return new Command("keygen")
    .description(
      "Generate an EdDSA signing keypair for appcast updates (one-time, per project)"
    )
    .action(async () => {
      const generateKeysBin = path.join(SPARKLE_DIR, "bin", "generate_keys");
      if (!existsSync(generateKeysBin)) {
        error(
          "generate_keys not found. Run `npm run sparkle:fetch` first."
        );
        process.exit(1);
      }
      log("Sparkle stores the private key in your login keychain.");
      log("The matching public key will be printed; copy it into Info.plist (SUPublicEDKey).");
      const code = await runStreaming(generateKeysBin, []);
      if (code !== 0) {
        error(`generate_keys exited with code ${code}.`);
        process.exit(1);
      }
    });
}

function makeSignCommand(): Command {
  return new Command("sign")
    .description("Compute the EdDSA signature of an artifact (for appcast.xml)")
    .argument("<artifact>", "Path to the artifact (e.g. dist/dao-browser-*.dmg)")
    .action(async (artifact: string) => {
      const signUpdateBin = path.join(SPARKLE_DIR, "bin", "sign_update");
      if (!existsSync(signUpdateBin)) {
        error("sign_update not found. Run `npm run sparkle:fetch` first.");
        process.exit(1);
      }
      if (!existsSync(artifact)) {
        error(`Artifact not found: ${artifact}`);
        process.exit(1);
      }
      const code = await runStreaming(signUpdateBin, [artifact]);
      if (code !== 0) {
        error(`sign_update exited with code ${code}.`);
        process.exit(1);
      }
    });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function downloadFile(url: string, dest: string): Promise<void> {
  // Prefer curl over node:fetch — it handles redirects, retries, and large
  // files much better, and it's always present on macOS.
  const code = await runStreaming("curl", [
    "-fL",
    "--retry",
    "3",
    "--retry-delay",
    "2",
    "-o",
    dest,
    url,
  ]);
  if (code !== 0) {
    error(`curl exited with code ${code} downloading ${url}`);
    process.exit(1);
  }
  if (!existsSync(dest) || statSync(dest).size === 0) {
    error(`Download produced empty file at ${dest}`);
    process.exit(1);
  }
}

async function sha256File(p: string): Promise<string> {
  const hash = createHash("sha256");
  await pipeline(createReadStream(p), hash);
  return hash.digest("hex");
}

function looksLikePlaceholder(hex: string): boolean {
  if (hex.length !== 64) return true;
  // All same char (e.g. "aaaa..." or "0000...").
  if (/^(.)\1+$/.test(hex)) return true;
  // Obviously fake patterns we used in dao.json scaffolding.
  if (/^a2f3835886c89cda/i.test(hex)) return true;
  return false;
}

function readVersionStamp(): string | null {
  if (!existsSync(VERSION_STAMP)) return null;
  try {
    return readFileSync(VERSION_STAMP, "utf-8").trim();
  } catch {
    return null;
  }
}

function writeVersionStamp(version: string): void {
  writeFileSync(VERSION_STAMP, version + "\n");
}

// Make ROOT_DIR import used (silences unused-var lint if ever enabled).
void ROOT_DIR;
