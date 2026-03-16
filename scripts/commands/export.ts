import { Command } from "commander";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { glob } from "glob";
import path from "node:path";
import {
  ENGINE_DIR,
  PATCHES_DIR,
  log,
  success,
  warn,
  error,
  run,
} from "../utils.js";

export const exportCommand = new Command("export")
  .description("Generate patch files from modifications in the Chromium tree")
  .argument("[files...]", "Specific files to export patches for")
  .option("--all", "Re-export all tracked patches")
  .action(async (files: string[], opts: { all?: boolean }) => {
    const srcDir = path.join(ENGINE_DIR, "src");

    if (!existsSync(srcDir)) {
      error("engine/src not found. Run 'npm run download' first.");
      process.exit(1);
    }

    if (opts.all) {
      log("Re-exporting all existing patches...");
      const patchFiles = await glob("**/*.patch", { cwd: PATCHES_DIR });

      for (const patchFile of patchFiles) {
        // Derive the original file path from the patch filename
        // e.g. chrome/browser/ui/BUILD.gn.patch -> chrome/browser/ui/BUILD.gn
        const originalFile = patchFile.replace(/\.patch$/, "");
        await exportSingleFile(srcDir, originalFile);
      }
      return;
    }

    if (files.length === 0) {
      // Export all unstaged changes
      log("Exporting all unstaged changes...");
      let diffOutput: string;
      try {
        diffOutput = run("git diff --name-only", { cwd: srcDir, silent: true });
      } catch {
        warn("No changes detected in engine/src");
        return;
      }

      if (!diffOutput.trim()) {
        warn("No changes detected in engine/src");
        return;
      }

      const changedFiles = diffOutput.split("\n").filter(Boolean);
      for (const file of changedFiles) {
        await exportSingleFile(srcDir, file);
      }
    } else {
      for (const file of files) {
        await exportSingleFile(srcDir, file);
      }
    }
  });

async function exportSingleFile(
  srcDir: string,
  filePath: string
): Promise<void> {
  const patchRelPath = filePath + ".patch";
  const patchFullPath = path.join(PATCHES_DIR, patchRelPath);

  let diff: string;
  try {
    diff = run(`git diff -- "${filePath}"`, { cwd: srcDir, silent: true });
  } catch {
    error(`Failed to generate diff for: ${filePath}`);
    return;
  }

  if (!diff.trim()) {
    warn(`No changes in: ${filePath}`);
    return;
  }

  mkdirSync(path.dirname(patchFullPath), { recursive: true });
  writeFileSync(patchFullPath, diff + "\n");
  success(`Exported: ${patchRelPath}`);
}
