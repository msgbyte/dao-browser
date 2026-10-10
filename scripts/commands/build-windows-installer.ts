import {execFileSync} from "node:child_process";
import {createHash} from "node:crypto";
import {copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync} from "node:fs";
import path from "node:path";
import {ROOT_DIR, log, runStreaming} from "../utils.js";
import type {WindowsCrossToolchain} from "../windows-toolchain.js";

const SDK_VERSION = "1.0.4258.31";
const SDK_SHA256 = "56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9";
const REBUILD_COMMAND = "npm run rebuild -- -- --release --target dao_installer_ui --platform windows";
const NATIVE_KEYS = ["windowTitle", "installingClose", "hostFailed", "hostCompleted", "launchFailed"];
const HOST_INPUTS = [
  "scripts/commands/build-windows-installer.ts",
  "scripts/windows-toolchain.ts",
  "scripts/windows-installer/native/host.cc",
  "scripts/windows-installer/native/host.manifest",
  "branding/win/dao.ico",
  "scripts/windows-installer/web/locales/en.json",
  "scripts/windows-installer/web/locales/zh-CN.json",
];

function sha256(content: Buffer): string {
  return createHash("sha256").update(content).digest("hex");
}

function inputHashes(): Record<string, string> {
  return Object.fromEntries(HOST_INPUTS.map(file => [file, sha256(readFileSync(path.join(ROOT_DIR, file)))]));
}

/** Packaging never implicitly rebuilds or accepts a host from older sources. */
export function getWindowsInstallerHost(): string {
  const directory = path.join(ROOT_DIR, ".dao/installer");
  const executable = path.join(directory, "dao-installer-ui.exe");
  try {
    const metadata = JSON.parse(readFileSync(path.join(directory, "build.json"), "utf8"));
    const current = inputHashes();
    if (metadata.version !== 1 || metadata.sdkVersion !== SDK_VERSION ||
        Object.keys(current).some(file => metadata.inputs?.[file] !== current[file]) ||
        metadata.executableSha256 !== sha256(readFileSync(executable)) ||
        metadata.licenseSha256 !== sha256(readFileSync(path.join(directory, "WebView2-LICENSE.txt")))) {
      throw new Error("Build inputs or outputs changed.");
    }
    return executable;
  } catch (cause) {
    throw new Error(`Windows installer host is missing or stale. Run ${REBUILD_COMMAND}, then package again.`, {cause});
  }
}

/** Inline all UI assets so the native host needs no server or remote requests. */
export function renderWindowsInstallerHtml(): string {
  const web = path.join(ROOT_DIR, "scripts/windows-installer/web");
  const locales = Object.fromEntries(["en", "zh-CN"].map(language =>
    [language, JSON.parse(readFileSync(path.join(web, "locales", `${language}.json`), "utf8"))]));
  const assets: Record<string, string> = {
    STYLE: readFileSync(path.join(web, "style.css"), "utf8"),
    SCRIPT: readFileSync(path.join(web, "app.js"), "utf8"),
    LOCALES: JSON.stringify(locales).replace(/</g, "\\u003c").replace(/\u2028/g, "\\u2028").replace(/\u2029/g, "\\u2029"),
    ICON_DATA_URL: `data:image/png;base64,${readFileSync(path.join(web, "assets/dao-logo.png")).toString("base64")}`,
  };
  if (/<\/style/i.test(assets.STYLE) || /<\/script/i.test(assets.SCRIPT)) {
    throw new Error("Installer assets contain an unsafe inline closing tag.");
  }
  const template = readFileSync(path.join(web, "index.html"), "utf8");
  for (const token of Object.keys(assets)) {
    if (!template.includes(`{{${token}}}`)) throw new Error(`Installer HTML is missing {{${token}}}.`);
  }
  return template.replace(/\{\{(STYLE|SCRIPT|LOCALES|ICON_DATA_URL)\}\}/g, (_match, token: string) => assets[token]);
}

function latestDirectory(parent: string, matches: (directory: string) => boolean): string {
  const versions = existsSync(parent) ? readdirSync(parent, {withFileTypes: true})
    .filter(entry => entry.isDirectory() && /^\d+(\.\d+)+$/.test(entry.name))
    .map(entry => entry.name).sort((a, b) => b.localeCompare(a, "en", {numeric: true})) : [];
  const version = versions.find(candidate => matches(path.join(parent, candidate)));
  if (!version) throw new Error(`No compatible Windows build tools found in ${parent}.`);
  return version;
}

function discoverToolchain() {
  const programFiles = process.env["ProgramFiles(x86)"] ?? "C:/Program Files (x86)";
  const vswhere = path.join(programFiles, "Microsoft Visual Studio/Installer/vswhere.exe");
  if (!existsSync(vswhere)) throw new Error("Visual Studio 2022 C++ build tools and the Windows SDK are required.");
  const visualStudio = execFileSync(vswhere, ["-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
  {encoding: "utf8", windowsHide: true}).trim();
  if (!visualStudio) throw new Error("Visual Studio C++ x64 build tools are required.");
  const msvcRoot = path.join(visualStudio, "VC/Tools/MSVC");
  const msvc = path.join(msvcRoot, latestDirectory(msvcRoot,
    directory => existsSync(path.join(directory, "bin/Hostx64/x64/cl.exe"))));
  let sdk = path.join(programFiles, "Windows Kits/10");
  try {
    const registry = execFileSync("reg.exe", ["query", "HKLM\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
      "/v", "KitsRoot10", "/reg:32"], {encoding: "utf8", windowsHide: true, stdio: ["ignore", "pipe", "ignore"]});
    sdk = registry.match(/KitsRoot10\s+REG_SZ\s+(.+)/)?.[1].trim() ?? sdk;
  } catch {
    // A standard SDK installation is sufficient when the registry key is absent.
  }
  const sdkVersion = latestDirectory(path.join(sdk, "Lib"), directory => {
    const version = path.basename(directory);
    return [path.join(directory, "um/x64/kernel32.lib"), path.join(directory, "ucrt/x64/ucrt.lib"),
      path.join(sdk, "bin", version, "x64/rc.exe"), path.join(sdk, "Include", version, "um/Windows.h")].every(existsSync);
  });
  const bin = path.join(msvc, "bin/Hostx64/x64");
  const env = {...process.env};
  for (const key of Object.keys(env)) if (key.toLowerCase() === "path") delete env[key];
  env.PATH = `${bin};${process.env.PATH ?? process.env.Path ?? ""}`;
  env.INCLUDE = [path.join(msvc, "include"), ...["ucrt", "shared", "um", "winrt"]
    .map(part => path.join(sdk, "Include", sdkVersion, part))].join(";");
  env.LIB = [path.join(msvc, "lib/x64"), ...["ucrt", "um"]
    .map(part => path.join(sdk, "Lib", sdkVersion, part, "x64"))].join(";");
  return {compiler: path.join(bin, "cl.exe"), resources: path.join(sdk, "bin", sdkVersion, "x64/rc.exe"), env};
}

async function webViewSdk(signal?: AbortSignal): Promise<string> {
  const tools = path.join(ROOT_DIR, ".dao/tools");
  mkdirSync(tools, {recursive: true});
  const archive = path.join(tools, `microsoft.web.webview2.${SDK_VERSION}.zip`);
  if (!existsSync(archive)) {
    log(`Downloading WebView2 SDK ${SDK_VERSION}...`);
    const response = await fetch(`https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${SDK_VERSION}/microsoft.web.webview2.${SDK_VERSION}.nupkg`, {signal});
    if (!response.ok) throw new Error(`WebView2 SDK download failed: HTTP ${response.status}.`);
    const data = Buffer.from(await response.arrayBuffer());
    if (sha256(data) !== SDK_SHA256) throw new Error("WebView2 SDK checksum mismatch.");
    signal?.throwIfAborted();
    writeFileSync(archive, data);
  }
  if (sha256(readFileSync(archive)) !== SDK_SHA256) {
    throw new Error(`WebView2 SDK checksum mismatch. Remove ${archive} and retry the rebuild.`);
  }
  const sdk = path.join(tools, `webview2-${SDK_VERSION}`);
  const quote = (value: string) => `'${value.replace(/'/g, "''")}'`;
  const code = process.platform === "win32"
    ? await runStreaming("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command",
      `$ErrorActionPreference = 'Stop'; Expand-Archive -LiteralPath ${quote(archive)} -DestinationPath ${quote(sdk)} -Force`], {signal})
    : await runStreaming("unzip", ["-q", "-o", archive, "-d", sdk], {signal});
  if (code !== 0) throw new Error(`WebView2 SDK extraction failed (exit ${code}).`);
  return sdk;
}

function nativeStrings(): string {
  const locales = ["en", "zh-CN"].map(language => JSON.parse(readFileSync(path.join(ROOT_DIR,
    "scripts/windows-installer/web/locales", `${language}.json`), "utf8")));
  const cases = NATIVE_KEYS.map(key => {
    const values = locales.map(locale => {
      if (typeof locale[key] !== "string" || !locale[key]) throw new Error(`Missing native installer string: ${key}.`);
      return `L${JSON.stringify(locale[key])}`;
    });
    return `  if (wcscmp(key, L"${key}") == 0) return chinese ? ${values[1]} : ${values[0]};`;
  });
  return `#pragma once\n#include <cwchar>\nnamespace dao {\ninline const wchar_t* NativeString(const wchar_t* key, bool chinese) {\n${cases.join("\n")}\n  return L"";\n}\n}  // namespace dao\n`;
}

/** Use Chromium's host tools with the same hermetic Windows SDK as the browser. */
export function createCrossInstallerBuildCommands(toolchain: WindowsCrossToolchain, sdk: string, staging: string) {
  const object = path.join(staging, "host.obj");
  const res = path.join(staging, "host.res");
  return [
    {command: "python3", args: [toolchain.rcScript, "/nologo", `/fo${res}`,
      ...toolchain.includeDirs.map(directory => `-imsvc${directory}`), path.join(staging, "host.rc")]},
    {command: toolchain.clangCl, args: ["--target=x86_64-pc-windows-msvc", "/nologo", "/c", "/MT", "/std:c++17",
      "/EHsc", "/utf-8", "/O2", "/W4", "/X", "/DUNICODE", "/D_UNICODE", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX",
      ...toolchain.includeDirs.map(directory => `-imsvc${directory}`), "/I", path.join(staging, "include"),
      "/I", path.join(sdk, "build/native/include"), `/Fo${object}`,
      "--", path.join(ROOT_DIR, "scripts/windows-installer/native/host.cc")]},
    {command: toolchain.lldLink, args: ["/SUBSYSTEM:WINDOWS", "/MACHINE:X64", "/MANIFEST:NO", "/INCREMENTAL:NO",
      `/OUT:${path.join(staging, "dao-installer-ui.exe")}`, ...toolchain.libDirs.map(directory => `/LIBPATH:${directory}`),
      object, res, path.join(sdk, "build/native/x64/WebView2LoaderStatic.lib"), "kernel32.lib", "user32.lib", "shell32.lib",
      "shlwapi.lib", "ole32.lib", "oleaut32.lib", "uuid.lib", "advapi32.lib", "version.lib", "comctl32.lib", "userenv.lib", "dwmapi.lib"]},
  ];
}

/** Invoked exclusively by the standalone dao_installer_ui rebuild target. */
export async function buildWindowsInstallerHost(signal?: AbortSignal): Promise<void> {
  signal?.throwIfAborted();
  const crossCompile = process.platform === "darwin" && ["arm64", "x64"].includes(process.arch);
  if (!crossCompile && (process.platform !== "win32" || process.arch !== "x64")) {
    throw new Error("The Windows installer host requires Windows x64 or macOS arm64/x64.");
  }
  const crossToolchain = crossCompile
    ? await (await import("../windows-toolchain.js")).prepareWindowsCrossToolchain(signal) : undefined;
  const nativeToolchain = crossCompile ? undefined : discoverToolchain();
  const inputs = inputHashes();
  const sdk = await webViewSdk(signal);
  const output = path.join(ROOT_DIR, ".dao/installer");
  mkdirSync(output, {recursive: true});
  const staging = mkdtempSync(path.join(output, ".build-"));
  try {
    const include = path.join(staging, "include");
    mkdirSync(include);
    writeFileSync(path.join(include, "native_strings.h"), nativeStrings());
    const resource = path.join(staging, "host.rc");
    const resourcePath = (file: string) => JSON.stringify(path.join(ROOT_DIR, file).replace(/\\/g, "/"));
    writeFileSync(resource, `\uFEFF101 ICON ${resourcePath("branding/win/dao.ico")}\n1 24 ${resourcePath("scripts/windows-installer/native/host.manifest")}\n`, "utf16le");
    const res = path.join(staging, "host.res");
    const executable = path.join(staging, "dao-installer-ui.exe");
    if (crossToolchain) {
      for (const step of createCrossInstallerBuildCommands(crossToolchain, sdk, staging)) {
        const code = await runStreaming(step.command, step.args, {cwd: staging, env: crossToolchain.env, signal});
        if (code !== 0) throw new Error(`Installer cross compilation failed: ${path.basename(step.command)} (exit ${code}).`);
      }
    } else if (nativeToolchain) {
      if (await runStreaming(nativeToolchain.resources, ["/nologo", "/fo", res, resource],
        {cwd: staging, env: nativeToolchain.env, signal}) !== 0) throw new Error("Installer resource compilation failed.");
      const code = await runStreaming(nativeToolchain.compiler, ["/nologo", "/MT", "/std:c++17", "/EHsc", "/utf-8", "/O2", "/W4",
        "/DUNICODE", "/D_UNICODE", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX", "/I", include, "/I", path.join(sdk, "build/native/include"),
        path.join(ROOT_DIR, "scripts/windows-installer/native/host.cc"), `/Fo${path.join(staging, "host.obj")}`, `/Fe${executable}`,
        "/link", "/SUBSYSTEM:WINDOWS", "/MACHINE:X64", "/MANIFEST:NO", "/INCREMENTAL:NO", res,
        path.join(sdk, "build/native/x64/WebView2LoaderStatic.lib"), "user32.lib", "shell32.lib", "shlwapi.lib", "ole32.lib",
        "oleaut32.lib", "uuid.lib", "advapi32.lib", "version.lib", "comctl32.lib", "userenv.lib", "dwmapi.lib"],
      {cwd: staging, env: nativeToolchain.env, signal});
      if (code !== 0) throw new Error(`Installer host compilation failed (exit ${code}).`);
    }
    if (!existsSync(executable)) throw new Error("Installer compiler did not produce dao-installer-ui.exe.");
    signal?.throwIfAborted();
    if (JSON.stringify(inputs) !== JSON.stringify(inputHashes())) throw new Error("Installer sources changed during compilation; rebuild again.");
    const license = path.join(staging, "WebView2-LICENSE.txt");
    writeFileSync(license, `${readFileSync(path.join(sdk, "LICENSE.txt"), "utf8")}\n${readFileSync(path.join(sdk, "NOTICE.txt"), "utf8")}`);
    writeFileSync(path.join(staging, "build.json"), JSON.stringify({version: 1, sdkVersion: SDK_VERSION, inputs,
      executableSha256: sha256(readFileSync(executable)), licenseSha256: sha256(readFileSync(license))}, null, 2) + "\n");
    const files = ["dao-installer-ui.exe", "WebView2-LICENSE.txt", "build.json"];
    const published: string[] = [];
    try {
      for (const file of files) {
        if (existsSync(path.join(output, file))) copyFileSync(path.join(output, file), path.join(staging, `${file}.previous`));
        renameSync(path.join(staging, file), path.join(output, file));
        published.push(file);
      }
    } catch (cause) {
      for (const file of published.reverse()) {
        const previous = path.join(staging, `${file}.previous`);
        if (existsSync(previous)) copyFileSync(previous, path.join(output, file));
        else rmSync(path.join(output, file), {force: true});
      }
      throw cause;
    }
    log(`Built Windows installer host: ${path.join(output, "dao-installer-ui.exe")}`);
  } finally {
    rmSync(staging, {recursive: true, force: true});
  }
}
