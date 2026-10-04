import {createHash} from "node:crypto";
import {copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync} from "node:fs";
import path from "node:path";

export function windowsInstallerName(version: string, debug = false): string {
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    throw new Error(`Invalid desktop release version: ${version}`);
  }
  return `dao-browser-${version}-windows-x64${debug ? "-debug" : ""}.exe`;
}

/** Copy Chromium's self-contained installer, never just chrome.exe. */
export function packageWindowsInstaller(
  outDir: string, distDir: string, version: string, debug = false,
): string {
  const source = path.join(outDir, "mini_installer.exe");
  if (!existsSync(source)) {
    throw new Error(`mini_installer.exe not found at ${source}. Build the mini_installer target first.`);
  }
  const artifact = path.join(distDir, windowsInstallerName(version, debug));
  mkdirSync(distDir, {recursive: true});
  copyFileSync(source, artifact);
  const digest = createHash("sha256").update(readFileSync(artifact)).digest("hex");
  writeFileSync(artifact + ".sha256", `${digest}  ${path.basename(artifact)}\n`);
  return artifact;
}
