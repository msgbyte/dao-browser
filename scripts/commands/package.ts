import { Command } from "commander";
import { existsSync, mkdirSync, rmSync, cpSync } from "node:fs";
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

    const stagingDir = path.join(DIST_DIR, ".staging");
    if (existsSync(stagingDir)) {
      rmSync(stagingDir, { recursive: true });
    }
    mkdirSync(stagingDir, { recursive: true });

    log(`Copying ${appName}.app to staging area...`);
    const stagedApp = path.join(stagingDir, `${appName}.app`);
    cpSync(appBundle, stagedApp, { recursive: true });

    if (opts.sign) {
      log("Applying ad-hoc code signature...");
      try {
        run(
          `codesign --force --sign - --deep "${stagedApp}"`,
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
      await createZip(stagingDir, appName, baseName);
    } else {
      await createDmg(stagingDir, appName, baseName);
    }

    rmSync(stagingDir, { recursive: true });
  });

async function createZip(
  stagingDir: string,
  appName: string,
  baseName: string
) {
  const zipPath = path.join(DIST_DIR, `${baseName}.zip`);
  if (existsSync(zipPath)) rmSync(zipPath);

  log(`Creating ${baseName}.zip ...`);
  run(`ditto -c -k --sequesterRsrc --keepParent "${appName}.app" "${zipPath}"`, {
    cwd: stagingDir,
  });
  success(`Created: dist/${baseName}.zip`);
}

async function createDmg(
  stagingDir: string,
  appName: string,
  baseName: string
) {
  const dmgPath = path.join(DIST_DIR, `${baseName}.dmg`);
  if (existsSync(dmgPath)) rmSync(dmgPath);

  log(`Creating ${baseName}.dmg ...`);

  const volumeName = appName;
  const dmgSize = estimateAppSize(stagingDir);

  run(
    `hdiutil create -volname "${volumeName}" -srcfolder "${stagingDir}" ` +
      `-ov -format UDZO -imagekey zlib-level=9 "${dmgPath}"`,
    { silent: true }
  );

  success(`Created: dist/${baseName}.dmg`);
}

function estimateAppSize(dir: string): string {
  try {
    const output = run(`du -sm "${dir}"`, { silent: true });
    const mb = parseInt(output.split("\t")[0], 10);
    return `${Math.ceil(mb * 1.2)}m`;
  } catch {
    return "2048m";
  }
}
