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
  THIRD_PARTY_DIR,
  ROOT_DIR,
  log,
  success,
  warn,
  error,
  run,
  loadConfig,
} from "../utils.js";
import {applyChromiumRewrites} from "../chromium-rewrites.js";

const execFileAsync = promisify(execFile);

function shellQuote(value: string): string {
  return `'${value.replace(/'/g, `'\\''`)}'`;
}

export function buildFixImportPatchesCommand(patchFiles: string[]): string {
  const args = patchFiles.map((patchFile) => {
    let repoRelativePatch = patchFile;
    if (path.isAbsolute(patchFile)) {
      repoRelativePatch = path.relative(ROOT_DIR, patchFile);
    } else if (!patchFile.startsWith(`src${path.sep}patches${path.sep}`) &&
               !patchFile.startsWith("src/patches/")) {
      repoRelativePatch = path.join("src", "patches", patchFile);
    }
    return shellQuote(repoRelativePatch);
  });
  return ["sh", "scripts/fix-import-patches.sh", ...args].join(" ");
}

export function buildFixImportPatchesMessage(patchFiles: string[]): string {
  return [
    "Repair failed patch targets with:",
    buildFixImportPatchesCommand(patchFiles),
    "Then re-run: npm run import",
  ].join("\n");
}

export const importCommand = new Command("import")
  .description("Apply patches and copy Dao code into the Chromium tree")
  .option("--patches-only", "Only apply patches, skip copying Dao source")
  .option(
    "--force",
    "Hard-reset engine/src to HEAD before importing (discards local edits to tracked files)"
  )
  .action(async (opts: { patchesOnly?: boolean; force?: boolean }) => {
    const srcDir = path.join(ENGINE_DIR, "src");

    if (!existsSync(srcDir)) {
      error("engine/src not found. Run 'npm run download' first.");
      process.exit(1);
    }

    // Step 0 (--force): reset any local modifications in engine/src so patches
    // apply cleanly. Only touches tracked files — untracked artifacts like
    // out/ and engine/src/dao/ are preserved.
    if (opts.force) {
      warn("Force mode: resetting engine/src tracked files to HEAD");
      try {
        run("git checkout -- .", { cwd: srcDir, silent: true });
        success("engine/src reset to HEAD");
      } catch (e) {
        error(`Failed to reset engine/src: ${(e as Error).message}`);
        process.exit(1);
      }
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
    const failedPatchFiles: string[] = [];

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
            failedPatchFiles.push(patchFile);
            failed++;
          }
        }
      }
    }

    log(
      `Patches: ${applied} applied, ${skipped} already applied, ${failed} failed`
    );

    // Step 1.25: Apply generated rewrites for highly mechanical Chromium edits
    // that would otherwise be tracked as dozens of one-line patch files.
    const rewriteSummary = applyChromiumRewrites(srcDir);
    if (rewriteSummary.filesChanged > 0) {
      success(
        `Generated Chromium rewrites: ${rewriteSummary.filesChanged} changed, ` +
          `${rewriteSummary.filesUnchanged} unchanged, ` +
          `${rewriteSummary.replacements} replacement(s)`
      );
    } else {
      log(
        `Generated Chromium rewrites: all ${rewriteSummary.filesUnchanged} ` +
          "file(s) up to date"
      );
    }
    if (rewriteSummary.filesMissing > 0) {
      warn(
        `Generated Chromium rewrites skipped ${rewriteSummary.filesMissing} ` +
          "missing file(s)"
      );
    }

    // Step 1.5: Inject Dao version into version_ui.cc
    const config = loadConfig();
    const daoVersion = config.version.display;
    const versionUiPath = path.join(
      srcDir,
      "chrome/browser/ui/webui/version/version_ui.cc"
    );
    if (existsSync(versionUiPath)) {
      const content = readFileSync(versionUiPath, "utf-8");
      const replacement =
        `u"(${daoVersion}) (chromium: " +\n` +
        `          base::UTF8ToUTF16(version_info::GetVersionNumber()) + u")",`;
      // Match either the pristine line or any previously-injected version so
      // re-importing after dao.json.version.display changes works idempotently.
      const injectedPattern =
        /u"\([^"]*\) \(chromium: " \+\s*base::UTF8ToUTF16\(version_info::GetVersionNumber\(\)\) \+ u"\)",/;
      const original = "base::UTF8ToUTF16(version_info::GetVersionNumber()),";
      if (injectedPattern.test(content)) {
        writeFileSync(
          versionUiPath,
          content.replace(injectedPattern, replacement)
        );
        success(`Re-injected Dao version ${daoVersion} into version_ui.cc`);
      } else if (content.includes(original)) {
        writeFileSync(versionUiPath, content.replace(original, replacement));
        success(`Injected Dao version ${daoVersion} into version_ui.cc`);
      }
    }

    // Step 1.6: Validate dao.json.version.display format.
    //
    // Historical note: this step used to rewrite chrome/VERSION with the
    // Dao version so Info.plist / Sparkle appcast would show "1.0.6" instead
    // of "147.0.7727.135". Don't do that — chrome/VERSION is consumed by
    // many Chromium build-time generators (policy, flag expiry, metrics,
    // field trials) which assume MAJOR is the real, monotonically increasing
    // Chromium version number. Stamping a small number there breaks those
    // generators (`kSensitivePolicies[]` goes empty, `kUnexpireFlagsM-1`
    // becomes an illegal C++ identifier, etc.).
    //
    // Instead, the display version is injected only at the Info.plist layer
    // via the --version flag on tweak_info_plist, wired through the
    // dao_display_version GN arg in configs/common.gn. Chromium internals
    // keep seeing the real Chromium version; users see the Dao version.
    {
      const parts = daoVersion.split(".").map((p) => p.trim());
      if (
        parts.length < 2 ||
        parts.length > 4 ||
        parts.some((p) => !/^\d+$/.test(p))
      ) {
        error(
          `dao.json.version.display is "${daoVersion}". Expected 2-4 numeric ` +
            `segments separated by dots (e.g. "1.0.6" or "1.0.6.1").`
        );
        process.exit(1);
      }
    }

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

      // Step 2.5: Mirror third_party/sparkle/ into engine/src/third_party/dao_sparkle/
      // so GN can reference Sparkle.framework via "//third_party/dao_sparkle/...".
      //
      // Sparkle.framework is a versioned bundle whose interior contains real
      // symlinks (Versions/Current -> A, Sparkle -> Versions/Current/Sparkle,
      // ...). Preserving those symlinks is critical: codesign refuses to seal
      // a framework whose Versions/Current is a directory rather than a link,
      // and at runtime dyld won't resolve `@rpath/Sparkle.framework/Sparkle`.
      //
      // smartCopySync (above) uses copyFileSync, which dereferences symlinks
      // and would flatten the framework. Use `ditto` instead — Apple's
      // recommended tool for copying app/framework bundles. It preserves
      // symlinks, extended attributes, and resource forks.
      const sparkleSrc = path.join(THIRD_PARTY_DIR, "sparkle");
      const sparkleDest = path.join(srcDir, "third_party", "dao_sparkle");
      const sparkleFwSrc = path.join(sparkleSrc, "Sparkle.framework");
      if (existsSync(sparkleFwSrc)) {
        log("Mirroring Sparkle framework into engine/src/third_party/dao_sparkle/ ...");
        mkdirSync(sparkleDest, { recursive: true });
        // ditto src dest copies the *contents* of src into dest. We want a
        // fresh mirror; clear the destination framework first if present.
        const sparkleFwDest = path.join(sparkleDest, "Sparkle.framework");
        if (existsSync(sparkleFwDest)) {
          run(`rm -rf "${sparkleFwDest}"`, { silent: true });
        }
        run(`ditto "${sparkleFwSrc}" "${sparkleFwDest}"`, { silent: true });
        success(
          "Synced third_party/sparkle/Sparkle.framework -> engine/src/third_party/dao_sparkle/Sparkle.framework"
        );
      } else {
        warn(
          "Sparkle framework not found at third_party/sparkle/Sparkle.framework. " +
            "Run 'npm run sparkle:fetch' before building, or auto-update will not " +
            "be linked into the app."
        );
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
      // The BRANDING manifest (product/company name, bundle id, copyright).
      // Dynamic copy replaces the old BRANDING.patch — previously the patch
      // locked MAC_BUNDLE_ID and friends, but any local tweak to engine's
      // BRANDING (e.g. swapping to a .debug bundle id for signing) would
      // make the patch stop applying. Driving the file from branding/ lets
      // us edit one source without fighting the patch system.
      "BRANDING": "BRANDING",
      "mac/app.icns": "mac/app.icns",
      "mac/document.icns": "mac/document.icns",
      "product_logo.svg": "product_logo.svg",
      "product_logo_16.png": "product_logo_16.png",
      "product_logo_24.png": "product_logo_24.png",
      "product_logo_32.png": "product_logo_32.png",
      "product_logo_48.png": "product_logo_48.png",
      "product_logo_64.png": "product_logo_64.png",
      "product_logo_128.png": "product_logo_128.png",
      "product_logo_256.png": "product_logo_256.png",
    };

    // Additional branding: scaled icons used by chrome://theme/ URLs
    // (e.g., settings page header logo)
    const themeBaseDir = path.join(srcDir, "chrome", "app", "theme");
    const scaledBrandingMap: Record<string, { src: string; size: number }> = {
      "default_100_percent/chromium/product_logo_16.png": {
        src: "product_logo_16.png",
        size: 16,
      },
      "default_100_percent/chromium/product_logo_32.png": {
        src: "product_logo_32.png",
        size: 32,
      },
      "default_200_percent/chromium/product_logo_16.png": {
        src: "product_logo_32.png",
        size: 32,
      },
      "default_200_percent/chromium/product_logo_32.png": {
        src: "product_logo_64.png",
        size: 64,
      },
    };

    // Dark mode SVG for settings header
    const darkSvgSrc = path.join(BRANDING_DIR, "product_logo_dark.svg");
    const darkSvgDest = path.join(
      srcDir,
      "ui",
      "webui",
      "resources",
      "images",
      "chrome_logo_dark.svg"
    );

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

    for (const [dest, info] of Object.entries(scaledBrandingMap)) {
      const srcPath = path.join(BRANDING_DIR, info.src);
      if (existsSync(srcPath)) {
        const destPath = path.join(themeBaseDir, dest);
        if (copyIfDifferent(srcPath, destPath)) {
          success(`Branding: ${info.src} -> chrome/app/theme/${dest}`);
          brandingCopied++;
        }
      }
    }

    if (existsSync(darkSvgSrc)) {
      if (copyIfDifferent(darkSvgSrc, darkSvgDest)) {
        success("Branding: product_logo_dark.svg -> ui/webui/resources/images/chrome_logo_dark.svg");
        brandingCopied++;
      }
    }

    if (brandingCopied > 0) {
      log(`Branding: ${brandingCopied} asset(s) copied`);
    } else {
      log("Branding: all assets up to date");
    }

    if (failed > 0) {
      error(`${failed} patch(es) failed to apply. Manual resolution needed.`);
      console.error(buildFixImportPatchesMessage(failedPatchFiles));
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
