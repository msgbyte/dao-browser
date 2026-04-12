import { Command } from "commander";
import { existsSync, mkdirSync, rmSync, cpSync, writeFileSync } from "node:fs";
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

  log(`Creating ${baseName}.dmg ...`);

  const iconPath = path.join(ROOT_DIR, "branding", "mac", "app.icns");
  const dmgSpec = {
    title: appName,
    icon: iconPath,
    "icon-size": 80,
    window: { size: { width: 540, height: 380 } },
    format: "UDZO",
    contents: [
      { x: 140, y: 190, type: "file", path: appBundle },
      { x: 400, y: 190, type: "link", path: "/Applications" },
    ],
  };

  const specPath = path.join(DIST_DIR, ".appdmg-spec.json");
  writeFileSync(specPath, JSON.stringify(dmgSpec, null, 2));

  try {
    const appdmgBin = path.join(ROOT_DIR, "node_modules", ".bin", "appdmg");
    run(`"${appdmgBin}" "${specPath}" "${dmgPath}"`);
    success(`Created: dist/${baseName}.dmg`);
  } finally {
    if (existsSync(specPath)) rmSync(specPath);
  }
}
