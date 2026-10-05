import {Command} from "commander";
import {existsSync} from "node:fs";
import path from "node:path";
import {ENGINE_DIR, loadConfig, resolveBuildTarget, runStreaming,
  type BuildTarget, type DaoConfig} from "../utils.js";
import {getAppName} from "./build.js";

export interface LaunchOptions {
  debug?: boolean;
  view?: boolean;
  little?: boolean;
  urls: string[];
}

export function createLaunchSpec(
  config: DaoConfig,
  target: BuildTarget,
  options: LaunchOptions,
  engineDir: string,
  localAppData?: string,
): {command: string; args: string[]} {
  const paths = target.os === "win" ? path.win32 : path.posix;
  const output = paths.join(engineDir, "src", "out", options.debug ? "dao-debug" : "dao");
  const name = getAppName(config.display_name, !!options.debug);
  const args: string[] = [];
  if (target.os === "win") {
    if (options.little) throw new Error("Little Dao external URL launch is not available on Windows yet.");
    if (!localAppData || !path.win32.isAbsolute(localAppData)) {
      throw new Error("LOCALAPPDATA must identify an absolute directory for Dao's profile.");
    }
    args.push(`--user-data-dir=${paths.join(localAppData, name, "User Data")}`);
  } else {
    args.push("--use-mock-keychain");
    const app = paths.join(output, `${name}.app`);
    if (options.little) {
      return {command: "open", args: ["-a", app, ...options.urls.slice(0, 1),
        "--args", ...args, ...options.urls.slice(1)]};
    }
    if (!options.debug) {
      return {command: "open", args: [app, "--args", ...args, ...options.urls]};
    }
  }
  if (options.debug) args.push("--enable-logging=stderr");
  if (options.debug && !options.view) args.push("--log-level=2");
  if (options.view) args.push("--enable-ui-devtools=9223", "--v=1");
  args.push(...options.urls);
  return {
    command: target.os === "win" ? paths.join(output, "chrome.exe") :
      paths.join(output, `${name}.app`, "Contents", "MacOS", name),
    args,
  };
}

export const startCommand = new Command("start")
  .description("Launch the local Dao Browser build")
  .allowUnknownOption()
  .option("--debug", "Launch the development build")
  .option("--view", "Enable UI developer tools")
  .option("--little", "Open the first URL through Little Dao on macOS")
  .argument("[urls...]", "URLs and Chromium switches to pass to the browser")
  .action(async (urls: string[], options: Omit<LaunchOptions, "urls">) => {
    const config = loadConfig();
    const target = resolveBuildTarget(config);
    if (target.os === 'win' && process.platform !== 'win32') {
      throw new Error('Run the Windows build on Windows. Unset DAO_BUILD_PLATFORM to launch the native macOS build.');
    }
    const spec = createLaunchSpec(config, target,
        {...options, urls}, ENGINE_DIR, process.env.LOCALAPPDATA);
    const output = spec.command === "open" ? spec.args[spec.args[0] === "-a" ? 1 : 0] : spec.command;
    if (!existsSync(output)) {
      throw new Error(`Browser build not found: ${output}. Run 'npm run rebuild' first for the development build.`);
    }
    process.exitCode = await runStreaming(spec.command, spec.args);
  });
