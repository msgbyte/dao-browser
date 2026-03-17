import { Command } from "commander";
import { existsSync, writeFileSync } from "node:fs";
import fsExtra from "fs-extra";
import { glob } from "glob";
import path from "node:path";
import {
  ENGINE_DIR,
  PATCHES_DIR,
  DAO_SRC_DIR,
  BRANDING_DIR,
  log,
  success,
  warn,
  error,
  run,
} from "../utils.js";

export const importCommand = new Command("import")
  .description("Apply patches and copy Dao code into the Chromium tree")
  .option("--patches-only", "Only apply patches, skip copying Dao source")
  .action(async (opts: { patchesOnly?: boolean }) => {
    const srcDir = path.join(ENGINE_DIR, "src");

    if (!existsSync(srcDir)) {
      error("engine/src not found. Run 'npm run download' first.");
      process.exit(1);
    }

    // Ensure engine/ has a CommonJS package.json so Chromium's Node.js
    // build tools are not affected by the root "type": "module" setting.
    const enginePkg = path.join(ENGINE_DIR, "package.json");
    if (!existsSync(enginePkg)) {
      writeFileSync(enginePkg, '{"type": "commonjs"}\n');
    }

    // Step 1: Apply patches
    log("Applying patches...");
    const patchFiles = await glob("**/*.patch", { cwd: PATCHES_DIR });

    if (patchFiles.length === 0) {
      warn("No patch files found in src/patches/");
    }

    let applied = 0;
    let failed = 0;

    for (const patchFile of patchFiles.sort()) {
      const fullPatchPath = path.join(PATCHES_DIR, patchFile);
      try {
        run(`git apply --check "${fullPatchPath}"`, {
          cwd: srcDir,
          silent: true,
        });
        run(`git apply "${fullPatchPath}"`, { cwd: srcDir, silent: true });
        success(`Applied: ${patchFile}`);
        applied++;
      } catch (e) {
        // Check if already applied
        try {
          run(`git apply --check --reverse "${fullPatchPath}"`, {
            cwd: srcDir,
            silent: true,
          });
          warn(`Already applied: ${patchFile}`);
          applied++;
        } catch {
          error(`Failed to apply: ${patchFile}`);
          failed++;
        }
      }
    }

    log(`Patches: ${applied} applied, ${failed} failed`);

    // Step 2: Copy Dao source code into Chromium tree
    if (!opts.patchesOnly) {
      log("Copying Dao source code...");

      if (existsSync(DAO_SRC_DIR)) {
        const destDir = path.join(srcDir, "dao");
        fsExtra.copySync(DAO_SRC_DIR, destDir, { overwrite: true });
        success(`Copied src/dao/ -> engine/src/dao/`);
      } else {
        warn("No Dao source directory found at src/dao/");
      }
    }

    // Step 3: Copy branding assets into Chromium theme directory
    log("Copying branding assets...");
    const chromiumThemeDir = path.join(
      srcDir,
      "chrome",
      "app",
      "theme",
      "chromium"
    );

    const brandingMap: Record<string, string> = {
      "mac/app.icns": "mac/app.icns",
      "mac/document.icns": "mac/document.icns",
      "product_logo_16.png": "product_logo_16.png",
      "product_logo_24.png": "product_logo_24.png",
      "product_logo_48.png": "product_logo_48.png",
      "product_logo_64.png": "product_logo_64.png",
      "product_logo_128.png": "product_logo_128.png",
      "product_logo_256.png": "product_logo_256.png",
    };

    let brandingCopied = 0;
    for (const [src, dest] of Object.entries(brandingMap)) {
      const srcPath = path.join(BRANDING_DIR, src);
      if (existsSync(srcPath)) {
        const destPath = path.join(chromiumThemeDir, dest);
        fsExtra.copySync(srcPath, destPath, { overwrite: true });
        success(`Branding: ${src} -> chrome/app/theme/chromium/${dest}`);
        brandingCopied++;
      }
    }

    if (brandingCopied > 0) {
      log(`Branding: ${brandingCopied} asset(s) copied`);
    } else {
      warn("No branding assets found in branding/");
    }

    if (failed > 0) {
      error(`${failed} patch(es) failed to apply. Manual resolution needed.`);
      process.exit(1);
    }

    success("Import complete");
  });
