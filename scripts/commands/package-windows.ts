import {createHash} from "node:crypto";
import {copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, statSync, writeFileSync} from "node:fs";
import path from "node:path";
import {ROOT_DIR, runStreaming, which} from "../utils.js";
import {getWindowsInstallerHost, renderWindowsInstallerHtml} from "./build-windows-installer.js";

export function windowsInstallerName(version: string, debug = false): string {
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    throw new Error(`Invalid desktop release version: ${version}`);
  }
  return `dao-browser-${version}-windows-x64${debug ? "-debug" : ""}.exe`;
}

/** Wrap Chromium's installer in the local WebView2 UI and native fallback. */
export async function packageWindowsInstaller(
  outDir: string, distDir: string, version: string, debug = false, signal?: AbortSignal,
): Promise<string> {
  signal?.throwIfAborted();
  const name = windowsInstallerName(version, debug);
  const source = path.join(outDir, "mini_installer.exe");
  if (!existsSync(source)) {
    throw new Error(`mini_installer.exe not found at ${source}. Build the mini_installer target first.`);
  }
  const setup = path.join(outDir, "setup.exe");
  if (!existsSync(setup) || !readFileSync(setup).includes(Buffer.from("dao-install-dir"))) {
    throw new Error("Native setup lacks custom installation directory support. " +
      "Run npm run rebuild -- -- --release --target mini_installer --platform windows, then package again.");
  }
  if (statSync(source).mtimeMs < statSync(setup).mtimeMs) {
    throw new Error("mini_installer.exe is older than setup.exe. Rebuild the mini_installer target first.");
  }
  const host = getWindowsInstallerHost();
  const html = renderWindowsInstallerHtml();
  const nativeWindows = process.platform === "win32";
  const compilerName = nativeWindows ? "makensis.exe" : "makensis";
  const compiler = [
    process.env.DAO_NSIS_DIR && path.join(process.env.DAO_NSIS_DIR, compilerName),
    which("makensis"),
    nativeWindows && path.join(ROOT_DIR, ".dao/tools/nsis-3.13/makensis.exe"),
    nativeWindows && process.env["ProgramFiles(x86)"] && path.join(process.env["ProgramFiles(x86)"]!, "NSIS/makensis.exe"),
    nativeWindows && process.env.ProgramFiles && path.join(process.env.ProgramFiles, "NSIS/makensis.exe"),
  ].find(candidate => candidate && existsSync(candidate));
  if (!compiler) {
    throw new Error("NSIS 3 is required to package Windows installers. Install NSIS or set DAO_NSIS_DIR to its directory.");
  }
  const artifact = path.join(distDir, name);
  mkdirSync(distDir, {recursive: true});
  const staging = mkdtempSync(path.join(distDir, ".windows-installer-"));
  try {
    const stagedArtifact = path.join(staging, name);
    const stagedHtml = path.join(staging, "index.html");
    writeFileSync(stagedHtml, html);
    const option = nativeWindows ? "/" : "-";
    const code = await runStreaming(compiler, [
      `${option}V2`, `${option}INPUTCHARSET`, "UTF8",
      `${option}DPAYLOAD=${path.resolve(source)}`, `${option}DOUTPUT=${path.resolve(stagedArtifact)}`,
      `${option}DVERSION=${version}`, `${option}DICON=${path.join(ROOT_DIR, "branding/win/dao.ico")}`,
      `${option}DWEBVIEW_HOST=${host}`, `${option}DWEBVIEW_HTML=${stagedHtml}`,
      `${option}DWEBVIEW_LICENSE=${path.join(path.dirname(host), "WebView2-LICENSE.txt")}`,
      path.join(ROOT_DIR, "scripts/windows-installer/installer.nsi"),
    ], {signal});
    signal?.throwIfAborted();
    if (code !== 0 || !existsSync(stagedArtifact)) {
      throw new Error(`NSIS packaging failed (exit ${code}).`);
    }
    const digest = createHash("sha256").update(readFileSync(stagedArtifact)).digest("hex");
    writeFileSync(stagedArtifact + ".sha256", `${digest}  ${name}\n`);
    // Keep both old outputs until both new outputs have been published.
    const published: string[] = [];
    for (const suffix of ["", ".sha256"]) {
      if (existsSync(artifact + suffix)) copyFileSync(artifact + suffix, stagedArtifact + suffix + ".previous");
    }
    try {
      for (const suffix of ["", ".sha256"]) {
        renameSync(stagedArtifact + suffix, artifact + suffix);
        published.push(suffix);
      }
    } catch (cause) {
      for (const suffix of published.reverse()) {
        const previous = stagedArtifact + suffix + ".previous";
        if (existsSync(previous)) copyFileSync(previous, artifact + suffix);
        else rmSync(artifact + suffix, {force: true});
      }
      throw cause;
    }
  } finally {
    rmSync(staging, {recursive: true, force: true});
  }
  return artifact;
}
