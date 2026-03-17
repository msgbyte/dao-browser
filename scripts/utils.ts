import { execSync, spawn, type SpawnOptions } from "node:child_process";
import { readFileSync } from "node:fs";
import path from "node:path";
import chalk from "chalk";

export const ROOT_DIR = path.resolve(import.meta.dirname, "..");
export const ENGINE_DIR = path.join(ROOT_DIR, "engine");
export const PATCHES_DIR = path.join(ROOT_DIR, "src", "patches");
export const DAO_SRC_DIR = path.join(ROOT_DIR, "src", "dao");
export const CONFIGS_DIR = path.join(ROOT_DIR, "configs");
export const BRANDING_DIR = path.join(ROOT_DIR, "branding");

export interface DaoConfig {
  name: string;
  display_name: string;
  version: {
    product: string;
    version: string;
    display: string;
  };
  build: {
    target_os: string;
    target_cpu: string;
  };
}

export function loadConfig(): DaoConfig {
  const configPath = path.join(ROOT_DIR, "dao.json");
  return JSON.parse(readFileSync(configPath, "utf-8"));
}

export function run(
  cmd: string,
  opts?: { cwd?: string; silent?: boolean }
): string {
  const cwd = opts?.cwd ?? ROOT_DIR;
  if (!opts?.silent) {
    console.log(chalk.dim(`$ ${cmd}`));
  }
  return execSync(cmd, { cwd, encoding: "utf-8", stdio: "pipe" }).trim();
}

export function runStreaming(
  cmd: string,
  args: string[],
  opts?: { cwd?: string; env?: NodeJS.ProcessEnv }
): Promise<number> {
  const cwd = opts?.cwd ?? ROOT_DIR;
  console.log(chalk.dim(`$ ${cmd} ${args.join(" ")}`));

  return new Promise((resolve, reject) => {
    const spawnOpts: SpawnOptions = {
      cwd,
      stdio: "inherit",
      shell: true,
      env: opts?.env ?? process.env,
    };
    const child = spawn(cmd, args, spawnOpts);
    child.on("close", (code) => resolve(code ?? 0));
    child.on("error", reject);
  });
}

export function which(binary: string): string | null {
  try {
    return execSync(`which ${binary}`, { encoding: "utf-8" }).trim();
  } catch {
    return null;
  }
}

export function log(msg: string): void {
  console.log(chalk.blue("dao") + " " + msg);
}

export function success(msg: string): void {
  console.log(chalk.green("✓") + " " + msg);
}

export function warn(msg: string): void {
  console.log(chalk.yellow("⚠") + " " + msg);
}

export function error(msg: string): void {
  console.error(chalk.red("✗") + " " + msg);
}
