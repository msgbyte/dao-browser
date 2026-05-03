import { Command } from "commander";
import { existsSync, mkdirSync, rmSync, cpSync, mkdtempSync } from "node:fs";
import os from "node:os";
import path from "node:path";
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

const DIST_DIR = path.join(ROOT_DIR, "dist");

export const packageCommand = new Command("package")
  .description("Package Dao Browser into a distributable .dmg or .zip")
  .option("--zip", "Create a .zip archive instead of .dmg")
  .option("--sign", "Apply ad-hoc code signature")
  .action(async (opts: { zip?: boolean; sign?: boolean }) => {
    const config = loadConfig();
    const appName = config.display_name;
    const version = config.version.display;
    const srcDir = path.join(ENGINE_DIR, "src");
    const outDir = path.join(srcDir, "out", "dao");
    const appBundle = path.join(outDir, `${appName}.app`);

    if (!existsSync(appBundle)) {
      error(
        `${appName}.app not found at ${appBundle}. Run 'npm run build' first.`
      );
      process.exit(1);
    }

    mkdirSync(DIST_DIR, { recursive: true });

    if (opts.sign) {
      log("Applying ad-hoc code signature...");
      try {
        run(
          `codesign --force --sign - --deep "${appBundle}"`,
          { silent: true }
        );
        success("Ad-hoc signed");
      } catch {
        warn("Code signing failed (non-critical, continuing)");
      }
    }

    const arch = config.build.target_cpu;
    const baseName = `dao-browser-${version}-mac-${arch}`;

    if (opts.zip) {
      await createZip(appBundle, appName, baseName);
    } else {
      await createDmg(appBundle, appName, baseName);
    }
  });

async function createZip(
  appBundle: string,
  appName: string,
  baseName: string
) {
  const zipPath = path.join(DIST_DIR, `${baseName}.zip`);
  if (existsSync(zipPath)) rmSync(zipPath);

  log(`Creating ${baseName}.zip ...`);
  run(`ditto -c -k --sequesterRsrc --keepParent "${appBundle}" "${zipPath}"`);
  success(`Created: dist/${baseName}.zip`);
}

async function createDmg(
  appBundle: string,
  appName: string,
  baseName: string
) {
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

    const args = [
      "--volname",
      quote(appName),
      ...(existsSync(iconPath) ? ["--volicon", quote(iconPath)] : []),
      "--window-size",
      "540",
      "380",
      "--icon-size",
      "80",
      "--icon",
      quote(`${appName}.app`),
      "140",
      "190",
      "--app-drop-link",
      "400",
      "190",
      "--format",
      "UDZO",
      "--hdiutil-quiet",
      "--no-internet-enable",
      quote(dmgPath),
      quote(stageDir),
    ];

    run(`"${createDmgBin}" ${args.join(" ")}`);
    success(`Created: dist/${baseName}.dmg`);
  } finally {
    rmSync(stageDir, { recursive: true, force: true });
  }
}

function quote(s: string): string {
  return `"${s.replace(/"/g, '\\"')}"`;
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
