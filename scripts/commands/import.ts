import { Command } from "commander";
import {
  existsSync,
  readFileSync,
  writeFileSync,
  readdirSync,
  statSync,
  mkdirSync,
  rmdirSync,
  copyFileSync,
  unlinkSync,
} from "node:fs";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
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

const execFileAsync = promisify(execFile);

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
    let skipped = 0;
    let failed = 0;

    const sortedPatches = patchFiles.sort();

    // Phase 1: parallel reverse-check to find already-applied patches
    const reverseResults = await Promise.allSettled(
      sortedPatches.map((patchFile) =>
        execFileAsync("git", [
          "apply", "--check", "--reverse",
          path.join(PATCHES_DIR, patchFile),
        ], { cwd: srcDir })
      )
    );

    const unapplied: string[] = [];
    for (let i = 0; i < sortedPatches.length; i++) {
      if (reverseResults[i].status === "fulfilled") {
        warn(`Already applied: ${sortedPatches[i]}`);
        skipped++;
      } else {
        unapplied.push(sortedPatches[i]);
      }
    }

    // Phase 2: batch-apply all unapplied patches at once
    if (unapplied.length > 0) {
      const fullPaths = unapplied.map((p) => path.join(PATCHES_DIR, p));
      try {
        run(
          `git apply ${fullPaths.map((p) => `"${p}"`).join(" ")}`,
          { cwd: srcDir, silent: true }
        );
        for (const p of unapplied) {
          success(`Applied: ${p}`);
        }
        applied += unapplied.length;
      } catch {
        // Batch failed — fallback to individual apply for precise error reporting
        for (const patchFile of unapplied) {
          const fullPatchPath = path.join(PATCHES_DIR, patchFile);
          try {
            run(`git apply "${fullPatchPath}"`, { cwd: srcDir, silent: true });
            success(`Applied: ${patchFile}`);
            applied++;
          } catch {
            error(`Failed to apply: ${patchFile}`);
            failed++;
          }
        }
      }
    }

    log(
      `Patches: ${applied} applied, ${skipped} already applied, ${failed} failed`
    );

    // Step 2: Copy Dao source code into Chromium tree (only changed files)
    if (!opts.patchesOnly) {
      log("Copying Dao source code...");

      if (existsSync(DAO_SRC_DIR)) {
        const destDir = path.join(srcDir, "dao");
        const stats = smartCopySync(DAO_SRC_DIR, destDir);
        success(
          `Synced src/dao/ -> engine/src/dao/ (${stats.copied} updated, ${stats.skipped} unchanged, ${stats.removed} removed)`
        );
      } else {
        warn("No Dao source directory found at src/dao/");
      }
    }

    // Step 3: Copy branding assets into Chromium theme directory (only changed)
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
        if (copyIfDifferent(srcPath, destPath)) {
          success(`Branding: ${src} -> chrome/app/theme/chromium/${dest}`);
          brandingCopied++;
        }
      }
    }

    if (brandingCopied > 0) {
      log(`Branding: ${brandingCopied} asset(s) copied`);
    } else {
      log("Branding: all assets up to date");
    }

    if (failed > 0) {
      error(`${failed} patch(es) failed to apply. Manual resolution needed.`);
      process.exit(1);
    }

    success("Import complete");
  });

function copyIfDifferent(src: string, dest: string): boolean {
  try {
    const srcStat = statSync(src);
    const destStat = statSync(dest);
    if (
      srcStat.size === destStat.size &&
      Buffer.compare(readFileSync(src), readFileSync(dest)) === 0
    ) {
      return false;
    }
  } catch {
    // dest doesn't exist — need to copy
  }
  mkdirSync(path.dirname(dest), { recursive: true });
  copyFileSync(src, dest);
  return true;
}

function smartCopySync(
  srcDir: string,
  destDir: string
): { copied: number; skipped: number; removed: number } {
  const stats = { copied: 0, skipped: 0, removed: 0 };

  const srcFiles = new Set<string>();

  function walkAndCopy(dir: string) {
    const entries = readdirSync(dir, { withFileTypes: true });
    for (const entry of entries) {
      const srcPath = path.join(dir, entry.name);
      const relPath = path.relative(srcDir, srcPath);
      const destPath = path.join(destDir, relPath);

      if (entry.isDirectory()) {
        walkAndCopy(srcPath);
      } else {
        srcFiles.add(relPath);
        if (copyIfDifferent(srcPath, destPath)) {
          stats.copied++;
        } else {
          stats.skipped++;
        }
      }
    }
  }

  walkAndCopy(srcDir);

  // Remove files in dest that no longer exist in src
  function walkAndClean(dir: string) {
    if (!existsSync(dir)) return;
    const entries = readdirSync(dir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = path.join(dir, entry.name);
      const relPath = path.relative(destDir, fullPath);
      if (entry.isDirectory()) {
        walkAndClean(fullPath);
        // Remove directory if now empty (rmdirSync throws on non-empty)
        try {
          rmdirSync(fullPath);
        } catch {}
      } else if (!srcFiles.has(relPath)) {
        unlinkSync(fullPath);
        stats.removed++;
      }
    }
  }

  walkAndClean(destDir);

  return stats;
}
