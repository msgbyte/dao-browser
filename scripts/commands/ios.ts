import { Command } from "commander";
import path from "node:path";
import { ROOT_DIR, runStreaming } from "../utils.js";

const directory = path.join(ROOT_DIR, "ios");

export const iosCommand = new Command("ios")
  .description("Build the independent iOS app without touching Chromium");

iosCommand.command("rebuild")
  .option("--simulator", "Build for iOS Simulator")
  .option("--core-only", "Run portable browser state tests only")
  .option("--sources-only", "Check app compilation without asset catalogs; does not produce a complete app")
  .action(async (options: { simulator?: boolean; coreOnly?: boolean; sourcesOnly?: boolean }) => {
    const run = async (command: string, args: string[]) => {
      const code = await runStreaming(command, args, { cwd: directory });
      if (code !== 0) process.exit(code);
    };
    await run("swift", ["test", "--package-path", "CoreTests"]);
    if (options.coreOnly) return;
    await run("xcodegen", ["generate", "--spec", "project.yml"]);
    const output = path.join(directory, options.sourcesOnly ? "build/SourceCheck" : "build");
    await run("xcodebuild", [
      "-project", "DaoBrowser.xcodeproj", "-target", "DaoBrowser",
      "-configuration", "Debug", "-sdk", options.simulator ? "iphonesimulator" : "iphoneos",
      `SYMROOT=${path.join(output, "Products")}`,
      `OBJROOT=${path.join(output, "Intermediates.noindex")}`,
      ...(options.sourcesOnly ? ["EXCLUDED_SOURCE_FILE_NAMES=Images.xcassets", "ASSETCATALOG_COMPILER_APPICON_NAME="] : []),
      "CODE_SIGNING_ALLOWED=NO", "build",
    ]);
  });
