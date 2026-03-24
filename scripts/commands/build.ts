import { Command } from "commander";
import { existsSync, readFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import {
  ENGINE_DIR,
  CONFIGS_DIR,
  loadConfig,
  log,
  success,
  warn,
  error,
  which,
  run,
  runStreaming,
} from "../utils.js";

export const buildCommand = new Command("build")
  .description("Build Dao Browser (gn gen + autoninja)")
  .option("--debug", "Build in debug mode")
  .option("--gen-only", "Only run gn gen, skip compilation")
  .option("-j <jobs>", "Number of parallel build jobs")
  .action(async (opts: { debug?: boolean; genOnly?: boolean; j?: string }) => {
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
      args += "is_debug = true\n";
      args += "is_component_build = true\n";
      args += "is_official_build = false\n";
      args += "use_lld = false\n";
    }

    mkdirSync(outDir, { recursive: true });
    writeFileSync(path.join(outDir, "args.gn"), args);
    success(`Written args.gn to out/${outName}/`);

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
    const ninjaArgs = ["-C", `out/${outName}`, "chrome"];
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
      fixDuplicateDylib(outDir);
    }

    success(`Build complete! Output: engine/src/out/${outName}/`);
  });

function fixDuplicateDylib(outDir: string) {
  const fwBinary = path.join(
    outDir,
    "Dao Browser.app/Contents/Frameworks/Dao Browser Framework.framework/Dao Browser Framework"
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
      run(`codesign --force --sign - --deep "${path.join(outDir, "Dao Browser.app")}"`, {
        silent: true,
      });
      success("Fixed lld duplicate dylib and re-signed");
    }
  } catch {
    warn("Could not apply duplicate dylib fix (non-critical)");
  }
}
