import { Command } from "commander";
import { spawnSync } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { ROOT_DIR } from "../utils.js";

const directory = path.join(ROOT_DIR, "ios");

export const iosCommand = new Command("ios")
  .description("Build the independent iOS app without touching Chromium");

iosCommand.command("rebuild")
  .option("--simulator", "Build for iOS Simulator")
  .option("--core-only", "Run portable browser state tests only")
  .option("--sources-only", "Check app compilation without asset catalogs; does not produce a complete app")
  .option("--archive", "Archive and export a signed App Store IPA using IOS_* environment variables")
  .action((options: { simulator?: boolean; coreOnly?: boolean; sourcesOnly?: boolean; archive?: boolean }) => {
    const run = (command: string, args: string[]) => {
      console.log(`$ ${command} ${args.map(arg => JSON.stringify(arg)).join(" ")}`);
      const result = spawnSync(command, args, { cwd: directory, stdio: "inherit" });
      if (result.error) throw result.error;
      if (result.status !== 0) throw new Error(`${command} failed (${result.signal ?? result.status})`);
    };
    const { IOS_TEAM_ID: team, IOS_PROFILE_UUID: profile, IOS_VERSION: version,
      IOS_BUILD_NUMBER: buildNumber } = process.env;
    if (options.archive) {
      if (options.simulator || options.coreOnly || options.sourcesOnly) {
        throw new Error("Cannot combine --archive with --simulator, --core-only, or --sources-only");
      }
      for (const [name, value, pattern] of [
        ["IOS_TEAM_ID", team, /^[A-Z0-9]{10}$/],
        ["IOS_PROFILE_UUID", profile, /^[A-Fa-f0-9]{8}(?:-[A-Fa-f0-9]{4}){3}-[A-Fa-f0-9]{12}$/],
        ["IOS_VERSION", version, /^\d+\.\d+\.\d+$/],
        ["IOS_BUILD_NUMBER", buildNumber, /^[1-9]\d{0,3}(?:\.\d{1,2}){0,2}$/],
      ] as const) {
        if (!value || !pattern.test(value)) throw new Error(`Missing or invalid ${name}`);
      }
    }
    run("swift", ["test", "--package-path", "CoreTests"]);
    if (options.coreOnly) return;
    run("xcodegen", ["generate", "--spec", "project.yml"]);
    if (options.archive) {
      const output = path.join(directory, "build/Release");
      const archive = path.join(output, "DaoBrowser.xcarchive");
      const exportOptions = path.join(output, "ExportOptions.plist");
      mkdirSync(output, { recursive: true });
      writeFileSync(exportOptions, `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>method</key><string>app-store-connect</string>
  <key>destination</key><string>export</string>
  <key>signingStyle</key><string>manual</string>
  <key>signingCertificate</key><string>Apple Distribution</string>
  <key>teamID</key><string>${team}</string>
  <key>provisioningProfiles</key><dict>
    <key>com.msgbyte.dao</key><string>${profile}</string>
  </dict>
  <key>manageAppVersionAndBuildNumber</key><false/>
  <key>testFlightInternalTestingOnly</key><false/>
  <key>uploadSymbols</key><true/>
</dict></plist>
`);
      run("xcodebuild", [
        "-project", "DaoBrowser.xcodeproj", "-scheme", "DaoBrowser",
        "-configuration", "Release", "-destination", "generic/platform=iOS",
        "-archivePath", archive, `DEVELOPMENT_TEAM=${team}`, "CODE_SIGN_STYLE=Manual",
        "CODE_SIGN_IDENTITY=Apple Distribution", `PROVISIONING_PROFILE_SPECIFIER=${profile}`,
        `MARKETING_VERSION=${version}`, `CURRENT_PROJECT_VERSION=${buildNumber}`, "archive",
      ]);
      run("xcodebuild", [
        "-exportArchive", "-archivePath", archive, "-exportOptionsPlist", exportOptions,
        "-exportPath", path.join(output, "export"),
      ]);
      return;
    }
    const output = path.join(directory, options.sourcesOnly ? "build/SourceCheck" : "build");
    run("xcodebuild", [
      "-project", "DaoBrowser.xcodeproj", "-target", "DaoBrowser",
      "-configuration", "Debug", "-sdk", options.simulator ? "iphonesimulator" : "iphoneos",
      `SYMROOT=${path.join(output, "Products")}`,
      `OBJROOT=${path.join(output, "Intermediates.noindex")}`,
      ...(options.sourcesOnly ? ["EXCLUDED_SOURCE_FILE_NAMES=Images.xcassets", "ASSETCATALOG_COMPILER_APPICON_NAME="] : []),
      "CODE_SIGNING_ALLOWED=NO", "build",
    ]);
  });
