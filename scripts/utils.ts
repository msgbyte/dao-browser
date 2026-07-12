import {
  execSync,
  spawn,
  type ChildProcess,
  type SpawnOptions,
} from "node:child_process";
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
  sparkle?: {
    version: string;
    url: string;
    sha256: string;
    feed_url: string;
  };
}

export const THIRD_PARTY_DIR = path.join(ROOT_DIR, "third_party");

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
  opts?: {
    cwd?: string;
    env?: NodeJS.ProcessEnv;
    signal?: AbortSignal;
    lifecycle?: ProcessLifecycleOptions;
  }
): Promise<number> {
  const cwd = opts?.cwd ?? ROOT_DIR;
  console.log(chalk.dim(`$ ${cmd} ${args.join(" ")}`));

  if (opts?.signal?.aborted) {
    return Promise.reject(createAbortError(opts.signal.reason));
  }

  const spawnOpts = createStreamingSpawnOptions(
    cwd,
    opts?.env ?? process.env,
    opts?.signal
  );
  const child = spawn(cmd, args, spawnOpts);
  return waitForSpawnedProcess(
    child,
    opts?.signal,
    opts?.lifecycle
  ).then((result) => {
    if (result.error) throw result.error;
    return result.code ?? 0;
  });
}

export function createStreamingSpawnOptions(
  cwd: string,
  env: NodeJS.ProcessEnv,
  signal?: AbortSignal,
  platform: NodeJS.Platform = process.platform
): SpawnOptions {
  const spawnOpts: SpawnOptions = {
    cwd,
    stdio: "inherit",
    shell: true,
    env,
  };
  if (signal && platform !== "win32") {
    spawnOpts.detached = true;
  }
  return spawnOpts;
}

export function createAbortError(reason: unknown): Error {
  const abortError = new Error("The operation was aborted.", { cause: reason });
  abortError.name = "AbortError";
  return abortError;
}

const PROCESS_TERMINATION_GRACE_MS = 150;
const PROCESS_TERMINATION_POLL_MS = 10;
const PROCESS_TERMINATION_TIMEOUT_MS = 3000;

export interface SpawnedProcessResult {
  code: number | null;
  error?: Error;
}

export interface ProcessLifecycleOptions {
  graceMs?: number;
  pollMs?: number;
  timeoutMs?: number;
  platform?: NodeJS.Platform;
  processGroupExists?: (processGroupId: number) => boolean;
  signalProcessGroup?: (
    processGroupId: number,
    signal: NodeJS.Signals
  ) => void;
  onAbandon?: () => void;
}

export class ProcessTerminationError extends Error {
  constructor(
    readonly processId: number,
    readonly processGroupId: number,
    detail?: string
  ) {
    super(
      detail ||
        `Release process group ${processGroupId} (leader PID ${processId}) ` +
          "may still be running after the termination timeout."
    );
    this.name = "ProcessTerminationError";
  }
}

export function waitForSpawnedProcess(
  child: ChildProcess,
  signal?: AbortSignal,
  lifecycle?: ProcessLifecycleOptions
): Promise<SpawnedProcessResult> {
  return new Promise((resolve, reject) => {
    let aborted = false;
    let leaderClosed = false;
    let groupGone = (lifecycle?.platform ?? process.platform) === "win32";
    let termination: ProcessTermination | undefined;
    let settled = false;
    const cleanup = () => {
      signal?.removeEventListener("abort", onAbort);
      termination?.cancel();
      child.off("close", onClose);
      child.off("error", onError);
    };
    const abandon = () => {
      cleanup();
      lifecycle?.onAbandon?.();
      child.unref();
    };
    const settleAbortIfComplete = () => {
      if (!aborted || !leaderClosed || !groupGone || settled) return;
      settled = true;
      cleanup();
      reject(createAbortError(signal?.reason));
    };
    const onAbort = () => {
      if (aborted || settled) return;
      aborted = true;
      termination = beginProcessTermination(child, lifecycle);
      void termination.completion.then(() => {
        groupGone = true;
        settleAbortIfComplete();
      }, (cause) => {
        if (settled) return;
        settled = true;
        abandon();
        reject(cause);
      });
    };
    const onClose = (code: number | null) => {
      leaderClosed = true;
      if (aborted) {
        settleAbortIfComplete();
        return;
      }
      if (settled) return;
      settled = true;
      cleanup();
      resolve({ code });
    };
    const onError = (error: Error) => {
      if (aborted) {
        if (!child.pid) leaderClosed = true;
        settleAbortIfComplete();
        return;
      }
      if (settled) return;
      settled = true;
      cleanup();
      resolve({ code: null, error });
    };
    child.once("close", onClose);
    child.once("error", onError);
    signal?.addEventListener("abort", onAbort, { once: true });
    if (signal?.aborted) onAbort();
  });
}

interface ProcessTermination {
  completion: Promise<void>;
  cancel: () => void;
}

function beginProcessTermination(
  child: ChildProcess,
  options: ProcessLifecycleOptions = {}
): ProcessTermination {
  if (!child.pid) {
    return { completion: Promise.resolve(), cancel: () => {} };
  }
  const platform = options.platform ?? process.platform;
  const graceMs = options.graceMs ?? PROCESS_TERMINATION_GRACE_MS;
  const pollMs = options.pollMs ?? PROCESS_TERMINATION_POLL_MS;
  const timeoutMs = options.timeoutMs ?? PROCESS_TERMINATION_TIMEOUT_MS;
  if (platform === "win32") {
    try {
      child.kill("SIGTERM");
    } catch {
      // The direct child already exited.
    }
    const killEscalation = setTimeout(() => {
      try {
        child.kill("SIGKILL");
      } catch {
        // The direct child exited during the TERM grace period.
      }
    }, graceMs);
    return {
      completion: Promise.resolve(),
      cancel: () => clearTimeout(killEscalation),
    };
  }

  const processGroupId = child.pid;
  const signalProcessGroup = options.signalProcessGroup ??
    ((groupId: number, signal: NodeJS.Signals) => {
      process.kill(-groupId, signal);
    });
  const groupExists = options.processGroupExists ?? processGroupExists;
  let pollTimer: NodeJS.Timeout | undefined;
  let cancelled = false;
  const startedAt = Date.now();
  let sentKill = false;
  try {
    signalProcessGroup(processGroupId, "SIGTERM");
  } catch {
    // The process group already exited.
  }
  const completion = new Promise<void>((resolve, reject) => {
    const poll = () => {
      if (cancelled) return;
      if (!groupExists(processGroupId)) {
        resolve();
        return;
      }
      const elapsed = Date.now() - startedAt;
      if (!sentKill && elapsed >= graceMs) {
        sentKill = true;
        try {
          signalProcessGroup(processGroupId, "SIGKILL");
        } catch {
          // The process group exited at the end of the grace period.
        }
      }
      if (elapsed >= timeoutMs) {
        reject(new ProcessTerminationError(child.pid!, processGroupId));
        return;
      }
      pollTimer = setTimeout(poll, pollMs);
    };
    poll();
  });
  return {
    completion,
    cancel: () => {
      cancelled = true;
      if (pollTimer) clearTimeout(pollTimer);
    },
  };
}

function processGroupExists(processGroupId: number): boolean {
  try {
    process.kill(-processGroupId, 0);
    return true;
  } catch (cause) {
    return !(
      cause instanceof Error &&
      "code" in cause &&
      cause.code === "ESRCH"
    );
  }
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
