import { Command } from "commander";
import { existsSync, readFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import {
  ENGINE_DIR,
  CONFIGS_DIR,
  BRANDING_DIR,
  loadConfig,
  log,
  success,
  warn,
  error,
  which,
  run,
  runStreaming,
} from "../utils.js";

const DEBUG_BUNDLE_ID_SUFFIX = ".debug";
const DEBUG_PRODUCT_NAME_SUFFIX = " Debug";

/**
 * App bundle name for the current build flavor. Release uses the project's
 * `display_name`; debug appends " Debug" so it can be installed alongside the
 * release build with its own user data directory and LaunchServices entry.
 */
export function getAppName(displayName: string, isDebug: boolean): string {
  return isDebug ? `${displayName}${DEBUG_PRODUCT_NAME_SUFFIX}` : displayName;
}

export const buildCommand = new Command("build")
  .description("Build Dao Browser (gn gen + autoninja)")
  .option("--debug", "Build in debug mode")
  .option("--gen-only", "Only run gn gen, skip compilation")
  .option("--target <target>", "Build target (default: chrome)", "chrome")
  .option("-j <jobs>", "Number of parallel build jobs")
  .action(async (opts: { debug?: boolean; genOnly?: boolean; target: string; j?: string }) => {
    const config = loadConfig();
    const srcDir = path.join(ENGINE_DIR, "src");
    const outName = opts.debug ? "dao-debug" : "dao";
    const outDir = path.join(srcDir, "out", outName);

    if (!existsSync(srcDir)) {
      error("engine/src not found. Run 'npm run download' first.");
      process.exit(1);
    }

    if (!which("gn")) {
      error("gn not found. Make sure depot_tools is in PATH.");
      process.exit(1);
    }

    // Merge GN args from config files
    log("Preparing build arguments...");
    const commonGn = path.join(CONFIGS_DIR, "common.gn");
    const platformGn = path.join(
      CONFIGS_DIR,
      `${config.build.target_os === "mac" ? "macos" : config.build.target_os}.gn`
    );

    let args = "";
    if (existsSync(commonGn)) {
      args += readFileSync(commonGn, "utf-8") + "\n";
    }
    if (existsSync(platformGn)) {
      args += readFileSync(platformGn, "utf-8") + "\n";
    }

    if (opts.debug) {
      // args += "is_debug = true\n"; // dont use debug mode as too slow.
      args += "is_component_build = true\n";
      args += "is_official_build = false\n";
      args += "use_lld = false\n";
    }

    // Sync Chromium's BRANDING so debug/release builds produce fully isolated
    // app bundles: distinct CFBundleIdentifier *and* distinct PRODUCT_FULLNAME
    // (the latter drives app bundle name, helper / framework names, and the
    // ~/Library/Application Support/<name>/ user data dir). Done idempotently
    // so alternating debug/release builds without re-import keep the correct
    // values.
    if (config.build.target_os === "mac") {
      syncMacBranding(srcDir, !!opts.debug, config.display_name);
    }

    mkdirSync(outDir, { recursive: true });
    const argsPath = path.join(outDir, "args.gn");
    const existing = existsSync(argsPath) ? readFileSync(argsPath, "utf-8") : null;
    if (existing !== args) {
      writeFileSync(argsPath, args);
      success(`Written args.gn to out/${outName}/`);
    } else {
      success(`args.gn unchanged, skipping write`);
    }

    // Run gn gen
    log("Running gn gen...");
    const gnCode = await runStreaming("gn", ["gen", `out/${outName}`], { cwd: srcDir });

    if (gnCode !== 0) {
      error("gn gen failed");
      process.exit(1);
    }
    success("gn gen complete");

    if (opts.genOnly) {
      return;
    }

    // Run autoninja
    log("Building Dao Browser...");
    const ninjaArgs = ["-C", `out/${outName}`, opts.target];
    if (opts.j) {
      ninjaArgs.unshift(`-j${opts.j}`);
    }

    // Strip AI-agent env vars so depot_tools/siso.py doesn't inject unsupported flags
    const cleanEnv = { ...process.env };
    for (const v of ["CURSOR_AGENT", "GEMINI_CLI", "CLAUDECODE", "CODEX_SANDBOX", "AI_AGENT"]) {
      delete cleanEnv[v];
    }

    const buildCmd = which("autoninja") ? "autoninja" : "ninja";
    const ninjaCode = await runStreaming(buildCmd, ninjaArgs, { cwd: srcDir, env: cleanEnv });

    if (ninjaCode !== 0) {
      error("Build failed");
      process.exit(1);
    }

    // Post-build: fix lld duplicate dylib issue on macOS component builds
    if (opts.debug && config.build.target_os === "mac") {
      const appName = getAppName(config.display_name, true);
      fixDuplicateDylib(outDir, appName);
    }

    success(`Build complete! Output: engine/src/out/${outName}/`);
  });

/**
 * Rewrite identity fields (PRODUCT_FULLNAME, PRODUCT_SHORTNAME,
 * PRODUCT_INSTALLER_*, MAC_BUNDLE_ID) in Chromium's BRANDING file so debug
 * builds get a fully separate app bundle. Release builds restore the original
 * values from the project's source-of-truth `branding/BRANDING`.
 *
 * Why the BRANDING file directly? `chrome_product_full_name` and
 * `chrome_mac_bundle_id` in `build/util/branding.gni` are derived from this
 * file via `version.py`, and neither is a `declare_args`, so they can't be
 * overridden via args.gn. The change lives entirely in engine/ (gitignored)
 * and is overwritten on every `npm run import`.
 */
function syncMacBranding(
  srcDir: string,
  isDebug: boolean,
  displayName: string
): void {
  const engineBranding = path.join(
    srcDir,
    "chrome/app/theme/chromium/BRANDING"
  );
  const projectBranding = path.join(BRANDING_DIR, "BRANDING");

  if (!existsSync(engineBranding) || !existsSync(projectBranding)) {
    warn("BRANDING file missing; skipping branding sync");
    return;
  }

  const projectContent = readFileSync(projectBranding, "utf-8");
  const projectFields = parseBrandingFields(projectContent);
  const baseBundleId = (projectFields.MAC_BUNDLE_ID ?? "")
    .replace(new RegExp(`${escapeRegExp(DEBUG_BUNDLE_ID_SUFFIX)}$`), "")
    .trim();
  if (!baseBundleId) {
    warn("MAC_BUNDLE_ID not found in branding/BRANDING; skipping");
    return;
  }
  const baseFullName = (
    projectFields.PRODUCT_FULLNAME ?? displayName
  )
    .replace(new RegExp(`${escapeRegExp(DEBUG_PRODUCT_NAME_SUFFIX)}$`), "")
    .trim();
  const baseShortName = (
    projectFields.PRODUCT_SHORTNAME ?? baseFullName
  )
    .replace(new RegExp(`${escapeRegExp(DEBUG_PRODUCT_NAME_SUFFIX)}$`), "")
    .trim();
  const baseInstallerFull = (
    projectFields.PRODUCT_INSTALLER_FULLNAME ?? `${baseFullName} Installer`
  )
    .replace(
      new RegExp(`${escapeRegExp(DEBUG_PRODUCT_NAME_SUFFIX)} Installer$`),
      " Installer"
    )
    .trim();
  const baseInstallerShort = (
    projectFields.PRODUCT_INSTALLER_SHORTNAME ?? `${baseShortName} Installer`
  )
    .replace(
      new RegExp(`${escapeRegExp(DEBUG_PRODUCT_NAME_SUFFIX)} Installer$`),
      " Installer"
    )
    .trim();

  const target: Record<string, string> = isDebug
    ? {
        MAC_BUNDLE_ID: `${baseBundleId}${DEBUG_BUNDLE_ID_SUFFIX}`,
        PRODUCT_FULLNAME: `${baseFullName}${DEBUG_PRODUCT_NAME_SUFFIX}`,
        PRODUCT_SHORTNAME: `${baseShortName}${DEBUG_PRODUCT_NAME_SUFFIX}`,
        PRODUCT_INSTALLER_FULLNAME: `${baseFullName}${DEBUG_PRODUCT_NAME_SUFFIX} Installer`,
        PRODUCT_INSTALLER_SHORTNAME: `${baseShortName}${DEBUG_PRODUCT_NAME_SUFFIX} Installer`,
      }
    : {
        MAC_BUNDLE_ID: baseBundleId,
        PRODUCT_FULLNAME: baseFullName,
        PRODUCT_SHORTNAME: baseShortName,
        PRODUCT_INSTALLER_FULLNAME: baseInstallerFull,
        PRODUCT_INSTALLER_SHORTNAME: baseInstallerShort,
      };

  const engineContent = readFileSync(engineBranding, "utf-8");
  let updated = engineContent;
  const changed: string[] = [];
  for (const [key, value] of Object.entries(target)) {
    const re = new RegExp(`^${escapeRegExp(key)}=.*$`, "m");
    if (!re.test(updated)) continue;
    const next = updated.replace(re, `${key}=${value}`);
    if (next !== updated) {
      changed.push(`${key}=${value}`);
      updated = next;
    }
  }

  if (updated !== engineContent) {
    writeFileSync(engineBranding, updated);
    log(`Branding (${isDebug ? "debug" : "release"}): ${changed.join(", ")}`);
  }
}

function parseBrandingFields(content: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const line of content.split(/\r?\n/)) {
    const m = line.match(/^([A-Z_]+)=(.*)$/);
    if (m) out[m[1]] = m[2];
  }
  return out;
}

function escapeRegExp(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function fixDuplicateDylib(outDir: string, appName: string) {
  const fwBinary = path.join(
    outDir,
    `${appName}.app/Contents/Frameworks/${appName} Framework.framework/${appName} Framework`
  );
  if (!existsSync(fwBinary)) return;

  const scriptPath = path.join(outDir, "_fix_dylib.py");
  writeFileSync(
    scriptPath,
    `
import struct, sys

path = sys.argv[1]
with open(path, 'rb') as f:
    data = bytearray(f.read())

magic = struct.unpack_from('<I', data, 0)[0]
if magic != 0xfeedfacf:
    sys.exit(0)

ncmds = struct.unpack_from('<I', data, 16)[0]
offset = 32
LC_LOAD_DYLIB = 0xc
LC_REEXPORT_DYLIB = 0x1f | 0x80000000
LC_LOAD_WEAK_DYLIB = 0x18 | 0x80000000

dylibs = {}
for i in range(ncmds):
    cmd = struct.unpack_from('<I', data, offset)[0]
    cmdsize = struct.unpack_from('<I', data, offset + 4)[0]
    if cmd in (LC_LOAD_DYLIB, LC_REEXPORT_DYLIB):
        name_off = struct.unpack_from('<I', data, offset + 8)[0]
        name_end = data.index(0, offset + name_off)
        name = data[offset + name_off:name_end].decode()
        if name not in dylibs:
            dylibs[name] = []
        dylibs[name].append((cmd, offset, cmdsize, name_off))
    offset += cmdsize

changed = False
for name, entries in dylibs.items():
    if len(entries) <= 1:
        continue
    has_reexport = any(e[0] == LC_REEXPORT_DYLIB for e in entries)
    if not has_reexport:
        continue
    for cmd_type, off, cs, no in entries:
        if cmd_type == LC_LOAD_DYLIB:
            struct.pack_into('<I', data, off, LC_LOAD_WEAK_DYLIB)
            ns = off + no
            avail = cs - no
            old = name.encode()
            new = (name.rsplit('.', 1)[0] + '_dup.dylib').encode()
            if len(new) + 1 <= avail:
                for j in range(avail):
                    data[ns + j] = 0
                for j, b in enumerate(new):
                    data[ns + j] = b
                changed = True

if changed:
    with open(path, 'wb') as f:
        f.write(data)
    print('fixed')
else:
    print('ok')
`
  );

  try {
    const result = run(`python3 "${scriptPath}" "${fwBinary}"`, { silent: true });
    if (result.includes("fixed")) {
      run(
        `codesign --force --sign - --deep "${path.join(outDir, `${appName}.app`)}"`,
        { silent: true }
      );
      success("Fixed lld duplicate dylib and re-signed");
    }
  } catch {
    warn("Could not apply duplicate dylib fix (non-critical)");
  }
}
