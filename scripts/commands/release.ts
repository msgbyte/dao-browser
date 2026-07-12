import { Command } from "commander";
import { spawn, spawnSync, type SpawnOptions } from "node:child_process";
import {
  copyFileSync,
  existsSync,
  readFileSync,
  readdirSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import chalk from "chalk";
import {
  ROOT_DIR,
  type DaoConfig,
  log,
  success,
  warn,
  error,
  runStreaming,
  createAbortError,
  ProcessTerminationError,
  waitForSpawnedProcess,
} from "../utils.js";
import {
  ReleaseTransaction,
  type ReleaseFileRestoreFailure,
  type ReleaseTagCleanupFailure,
} from "./release-transaction.js";

export interface ReleaseOptions {
  bump?: "patch" | "minor" | "major";
  bucket?: string;
  prefix?: string;
  skipUpload?: boolean;
  skipBuild?: boolean;
  skipBump?: boolean;
  // Default true. Set to false via --no-force-import to call
  // `cli.ts import` without --force, e.g. when iterating on a release
  // failure and you've already reset engine/src yourself.
  forceImport?: boolean;
  // Resume from staple: dmg has already been notarized externally
  // (e.g. you ran `xcrun stapler staple dist/...dmg` yourself), and
  // we just need to finish appcast + copy + upload.
  resumeFromStaple?: boolean;
  dryRun?: boolean;
}

export type ReleasePhase =
  | "preflight"
  | "version"
  | "import"
  | "build"
  | "package"
  | "notarize"
  | "staple"
  | "appcast"
  | "metadata"
  | "upload"
  | "tag"
  | "rollback";

export class ReleaseError extends Error {
  recovery?: ReleaseRecoveryState;
  terminationUncertain?: ReleaseTerminationUncertain;
  notarizationRecovery?: ReleaseNotarizationRecovery;

  constructor(
    readonly phase: ReleasePhase,
    message: string,
    options?: ErrorOptions
  ) {
    super(message, options);
    this.name = "ReleaseError";
  }
}

export interface ReleaseRecoveryState {
  failedPhase: ReleasePhase;
  oldVersion: string;
  newVersion: string;
  restored: string[];
  conflicts: string[];
  restoreFailures: ReleaseFileRestoreFailure[];
  tagCleanupFailure: ReleaseTagCleanupFailure | null;
  notarizationRecovery?: ReleaseNotarizationRecovery;
  terminationUncertain?: ReleaseTerminationUncertain;
  rollbackFailure?: string;
  retryCommand?: string;
}

export interface ReleaseNotarizationRecovery {
  command: string;
  detail: string;
}

export interface ReleaseTerminationUncertain {
  processId: number;
  processGroupId: number;
  detail: string;
}

const REQUIRED_UPLOAD_ENV = [
  "CLOUDFLARE_ACCOUNT_ID",
  "CLOUDFLARE_API_TOKEN",
] as const;

// Required for `package:release` (signs with Developer ID + notarizes).
// Notarization accepts EITHER a stored keychain profile OR a triple of
// apple-id/team-id/password — we check for at least one valid combination.
const SIGN_IDENTITY_ENV = "DAO_SIGN_IDENTITY";
const NOTARIZE_PROFILE_ENV = "DAO_NOTARIZE_KEYCHAIN_PROFILE";
const NOTARIZE_TRIPLE_ENV = [
  "DAO_NOTARIZE_APPLE_ID",
  "DAO_NOTARIZE_TEAM_ID",
  "DAO_NOTARIZE_PASSWORD",
] as const;

export const releaseCommand = new Command("release")
  .description(
    "End-to-end release: bump version, import, build, package:release, " +
      "generate appcast, copy appcast to website/public, upload .dmg to R2."
  )
  .option(
    "--bump <type>",
    "Version bump kind: patch | minor | major",
    (v: string): "patch" | "minor" | "major" => {
      if (v !== "patch" && v !== "minor" && v !== "major") {
        throw new Error(`Invalid --bump value: ${v}`);
      }
      return v;
    },
    "patch" as const
  )
  .option("-b, --bucket <name>", "R2 bucket (or $R2_BUCKET)")
  .option("-p, --prefix <prefix>", "R2 key prefix for the .dmg", "")
  .option("--skip-upload", "Skip the R2 upload step (still produces artifacts)")
  .option("--skip-build", "Skip import + build (use existing dist/ artifact)")
  .option(
    "--no-force-import",
    "Run `cli.ts import` without --force (default: forced). " +
      "Use this when engine/src is already in a known-good state and you " +
      "don't want the import step to reset local edits."
  )
  .option(
    "--skip-bump",
    "Use the version already in dao.json instead of bumping " +
      "(use when resuming a release that failed mid-way)"
  )
  .option(
    "--resume-from-staple",
    "Skip build + sign + notarize. Use this when you've already " +
      "notarized + stapled the dmg manually after a notarytool keychain " +
      "failure. Continues from generate_appcast onward."
  )
  .option("--dry-run", "Print steps without executing them")
  .action(async (options: ReleaseOptions) => {
    try {
      const result = await runReleaseWithSignals(options);
      printReleaseSuccess(result.newVersion);
    } catch (cause) {
      const failure = cause instanceof ReleaseError
        ? cause
        : new ReleaseError("preflight", String(cause), { cause });
      error(formatReleaseFailure(failure));
      process.exitCode = 1;
    }
  });

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

export interface ReleaseDependencies {
  rootDir: string;
  env: NodeJS.ProcessEnv;
  now: () => Date;
  head: () => string;
  tagState: (tagName: string) => ReleaseTagState;
  createTag: (tagName: string, commit: string) => string | Promise<string>;
  deleteTag: (tagName: string, expectedObjectId: string) => void;
  createTransaction?: (paths: string[]) => ReleaseTransaction;
  runPhase: (
    phase: ReleasePhase,
    context: ReleasePhaseContext
  ) => Promise<void>;
  signal?: AbortSignal;
}

export interface ReleasePhaseContext {
  options: ReleaseOptions;
  oldVersion: string;
  newVersion: string;
  releaseHead: string;
  dmgPath: string;
  appcastPath: string;
  signal?: AbortSignal;
}

export interface ReleaseResult {
  oldVersion: string;
  newVersion: string;
}

export type ReleaseCommandRunner = (
  cmd: string,
  args: string[],
  opts?: { signal?: AbortSignal }
) => Promise<number>;

function buildReleasePhaseContext(
  rootDir: string,
  options: ReleaseOptions,
  oldVersion: string,
  newVersion: string,
  releaseHead: string,
  config: DaoConfig,
  signal?: AbortSignal
): ReleasePhaseContext {
  const baseName =
    "dao-browser-" + newVersion + "-mac-" + config.build.target_cpu;
  return {
    options,
    oldVersion,
    newVersion,
    releaseHead,
    dmgPath: path.join(rootDir, "dist", baseName + ".dmg"),
    appcastPath: path.join(rootDir, "dist", "appcast.xml"),
    signal,
  };
}

function throwIfReleaseAborted(
  signal: AbortSignal | undefined,
  phase: ReleasePhase
): void {
  if (!signal?.aborted) return;
  const reason = signal.reason;
  const message = reason instanceof Error
    ? reason.message
    : String(reason || "Release interrupted.");
  throw new ReleaseError(phase, message, { cause: reason });
}

function normalizeReleaseFailure(
  cause: unknown,
  phase: ReleasePhase,
  signal?: AbortSignal
): ReleaseError {
  if (cause instanceof ReleaseError) return cause;
  if (cause instanceof ProcessTerminationError) {
    const failure = new ReleaseError(phase, cause.message, { cause });
    failure.terminationUncertain = {
      processId: cause.processId,
      processGroupId: cause.processGroupId,
      detail: cause.message,
    };
    return failure;
  }
  if (signal?.aborted) {
    const reason = signal.reason;
    const message = reason instanceof Error
      ? reason.message
      : String(reason || "Release interrupted.");
    return new ReleaseError(phase, message, { cause });
  }
  return new ReleaseError(phase, String(cause), { cause });
}

function yieldToPendingSignals(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

export function plannedReleasePhases(
  options: ReleaseOptions
): ReleasePhase[] {
  const phases: ReleasePhase[] = [];
  if (!options.skipBuild && !options.resumeFromStaple) {
    phases.push("import", "build", "package", "notarize", "staple");
  } else if (options.resumeFromStaple) {
    phases.push("staple");
  }
  phases.push("appcast", "metadata");
  if (!options.skipUpload) phases.push("upload");
  phases.push("tag");
  return phases;
}

function collectReleasePreflightProblems(
  options: ReleaseOptions,
  rootDir: string,
  env: NodeJS.ProcessEnv,
  config: DaoConfig
): string[] {
  const willUpload = !options.skipUpload;
  const willBuild = !options.skipBuild;
  const bucket = options.bucket || env.R2_BUCKET;
  const problems: string[] = [];
  void config;

  if (willBuild) {
    if (!env[SIGN_IDENTITY_ENV]) {
      problems.push(
        `Missing ${SIGN_IDENTITY_ENV} (required by package:release).\n` +
          '  Example: export DAO_SIGN_IDENTITY="Developer ID Application: Foo Bar (TEAMID1234)"\n' +
          "  List installed identities: security find-identity -v -p codesigning"
      );
    }
    const hasProfile = !!env[NOTARIZE_PROFILE_ENV];
    const tripleSet = NOTARIZE_TRIPLE_ENV.filter((key) => !!env[key]);
    if (!hasProfile && tripleSet.length === 0) {
      problems.push(
        "Missing notarization credentials (required by package:release).\n" +
          "  Set EITHER:\n" +
          `    ${NOTARIZE_PROFILE_ENV}=<profile-name>\n` +
          "    (created via: xcrun notarytool store-credentials <profile> ...)\n" +
          "  OR all three of:\n" +
          `    ${NOTARIZE_TRIPLE_ENV.join(", ")}`
      );
    } else if (
      !hasProfile &&
      tripleSet.length > 0 &&
      tripleSet.length < NOTARIZE_TRIPLE_ENV.length
    ) {
      const missingTriple = NOTARIZE_TRIPLE_ENV.filter((key) => !env[key]);
      problems.push(
        `Incomplete notarization credentials: missing ${missingTriple.join(
          ", "
        )}.\n` +
          "  Either set all three apple-id/team-id/password vars, or use " +
          `${NOTARIZE_PROFILE_ENV} instead.`
      );
    }
  }

  const generateAppcast = path.join(
    rootDir,
    "third_party",
    "sparkle",
    "bin",
    "generate_appcast"
  );
  if (!existsSync(generateAppcast)) {
    problems.push(
      "third_party/sparkle/bin/generate_appcast not found.\n" +
        "  Run `npm run sparkle:fetch` first."
    );
  }

  if (willUpload) {
    const missing = REQUIRED_UPLOAD_ENV.filter((key) => !env[key]);
    if (missing.length > 0) {
      problems.push(
        `Missing env var(s) required for R2 upload: ${missing.join(", ")}.\n` +
          "  Set them, or pass --skip-upload to produce artifacts only."
      );
    }
    if (!bucket) {
      problems.push(
        "R2 bucket not specified.\n" +
          "  Pass --bucket or set R2_BUCKET, or use --skip-upload."
      );
    }
  }

  const publicAppcast = path.join(rootDir, "website/public/appcast.xml");
  const appcastTemplate = path.join(rootDir, "branding/appcast.template.xml");
  if (!existsSync(publicAppcast) && !existsSync(appcastTemplate)) {
    problems.push(
      "Cannot seed dist/appcast.xml — neither\n" +
        `  ${path.relative(rootDir, publicAppcast)}\n` +
        "nor\n" +
        `  ${path.relative(rootDir, appcastTemplate)}\n` +
        "exists. Restore one of them and rerun."
    );
  }
  return problems;
}

function runReleasePreflight(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies,
  config: DaoConfig,
  tagName: string,
  releaseHead: string,
  tag: ReleaseTagState
): void {
  const problems = collectReleasePreflightProblems(
    options,
    dependencies.rootDir,
    dependencies.env,
    config
  );
  if (!/^[0-9a-f]{40}$/i.test(releaseHead)) {
    problems.push("Could not resolve a valid git HEAD — refusing to release.");
  }
  if (tag.exists && tag.commit !== releaseHead) {
    problems.push(
      "Tag " +
        tagName +
        " points at " +
        tag.commit +
        ", not HEAD (" +
        releaseHead +
        ")."
    );
  }
  if (problems.length) {
    throw new ReleaseError("preflight", problems.join("\n\n"));
  }
}

function readReleaseConfig(
  rootDir: string,
  snapshot: Buffer | null
): DaoConfig {
  const configPath = path.join(rootDir, "dao.json");
  try {
    if (!snapshot) throw new Error("file does not exist");
    const config = JSON.parse(snapshot.toString("utf-8")) as DaoConfig;
    if (
      !config?.version?.display ||
      !config.version.version ||
      !config?.build?.target_cpu
    ) {
      throw new Error("required release fields are missing");
    }
    return config;
  } catch (cause) {
    throw new ReleaseError(
      "preflight",
      `Failed to read ${path.relative(rootDir, configPath)}: ${String(cause)}`,
      { cause }
    );
  }
}

function readReleaseProvenance(
  dependencies: ReleaseDependencies,
  tagName: string
): { releaseHead: string; tag: ReleaseTagState } {
  try {
    return {
      releaseHead: dependencies.head(),
      tag: { ...dependencies.tagState(tagName) },
    };
  } catch (cause) {
    throw new ReleaseError(
      "preflight",
      "Failed to inspect release provenance: " + String(cause),
      { cause }
    );
  }
}

function equalTagState(
  left: ReleaseTagState,
  right: ReleaseTagState
): boolean {
  return (
    left.exists === right.exists &&
    (!left.exists || (
      left.commit === right.commit && left.objectId === right.objectId
    ))
  );
}

function revalidateReleaseProvenance(
  dependencies: ReleaseDependencies,
  tagName: string,
  releaseHead: string,
  initialTag: ReleaseTagState
): void {
  let currentHead: string;
  let currentTag: ReleaseTagState;
  try {
    currentHead = dependencies.head();
    currentTag = { ...dependencies.tagState(tagName) };
  } catch (cause) {
    throw new ReleaseError(
      "tag",
      "Failed to revalidate release provenance: " + String(cause),
      { cause }
    );
  }
  if (currentHead !== releaseHead) {
    throw new ReleaseError(
      "tag",
      `HEAD moved from ${releaseHead} to ${currentHead} during release.`
    );
  }
  if (!equalTagState(initialTag, currentTag)) {
    throw new ReleaseError(
      "tag",
      `Tag ${tagName} changed during release.`
    );
  }
}

function createReleaseTransaction(
  paths: string[],
  dependencies: ReleaseDependencies
): ReleaseTransaction {
  try {
    return dependencies.createTransaction
      ? dependencies.createTransaction(paths)
      : new ReleaseTransaction(paths);
  } catch (cause) {
    throw new ReleaseError(
      "preflight",
      "Failed to initialize release transaction: " + String(cause),
      { cause }
    );
  }
}

function applyCanonicalPhaseMutation(
  phase: ReleasePhase,
  context: ReleasePhaseContext,
  transaction: ReleaseTransaction,
  publicAppcast: string,
  infoPath: string,
  chromiumVersion: string,
  now: Date
): void {
  if (context.options.dryRun) return;
  if (phase === "appcast") {
    transaction.writeFile(publicAppcast, readFileSync(context.appcastPath));
  }
  if (phase === "metadata") {
    const originalInfo = transaction.originalFileContents(infoPath);
    if (!originalInfo) {
      throw new ReleaseError("metadata", `info.json not found: ${infoPath}`);
    }
    transaction.writeFile(infoPath, updateInfoJsonContents(
      originalInfo,
      {
        version: context.newVersion,
        chromiumVersion,
        releasedAt: formatReleaseDate(now),
      }
    ));
    success(
      `info.json → version ${context.newVersion}, ` +
        `releasedAt ${formatReleaseDate(now)}`
    );
  }
}

export async function runRelease(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies = defaultReleaseDependencies
): Promise<ReleaseResult> {
  const daoPath = path.join(dependencies.rootDir, "dao.json");
  const publicAppcast = path.join(
    dependencies.rootDir,
    "website/public/appcast.xml"
  );
  const infoPath = path.join(dependencies.rootDir, "website/public/info.json");
  const transaction = createReleaseTransaction(
    [daoPath, publicAppcast, infoPath],
    dependencies
  );
  const config = readReleaseConfig(
    dependencies.rootDir,
    transaction.originalFileContents(daoPath)
  );
  const oldVersion = config.version.display;
  const newVersion = options.skipBump
    ? oldVersion
    : bumpVersion(oldVersion, options.bump || "patch");
  const tagName = "v" + newVersion;
  const provenance = readReleaseProvenance(dependencies, tagName);

  runReleasePreflight(
    options,
    dependencies,
    config,
    tagName,
    provenance.releaseHead,
    provenance.tag
  );
  success("Pre-flight check passed");
  let currentPhase: ReleasePhase = "version";

  try {
    throwIfReleaseAborted(dependencies.signal, currentPhase);
    if (options.skipBump) {
      log(`Reusing version from dao.json: ${newVersion} (--skip-bump)`);
    } else {
      log(`Bumping version: ${oldVersion} → ${newVersion}`);
      if (!options.dryRun) {
        const originalDao = transaction.originalFileContents(daoPath);
        if (!originalDao) {
          throw new ReleaseError("version", "dao.json does not exist");
        }
        transaction.writeFile(
          daoPath,
          writeDaoVersionContents(originalDao, newVersion)
        );
        success(`dao.json version.display → ${newVersion}`);
      }
    }
    const context = buildReleasePhaseContext(
      dependencies.rootDir,
      options,
      oldVersion,
      newVersion,
      provenance.releaseHead,
      config,
      dependencies.signal
    );
    if (options.skipBuild && !options.resumeFromStaple) {
      warn("Skipping build/package (--skip-build)");
    }
    if (options.skipUpload) {
      warn("Skipping R2 upload (--skip-upload)");
    }
    for (const phase of plannedReleasePhases(options)) {
      currentPhase = phase;
      throwIfReleaseAborted(dependencies.signal, currentPhase);
      await dependencies.runPhase(phase, context);
      throwIfReleaseAborted(dependencies.signal, currentPhase);
      applyCanonicalPhaseMutation(
        phase,
        context,
        transaction,
        publicAppcast,
        infoPath,
        config.version.version,
        dependencies.now()
      );
    }
    currentPhase = "tag";
    await yieldToPendingSignals();
    throwIfReleaseAborted(dependencies.signal, currentPhase);
    revalidateReleaseProvenance(
      dependencies,
      tagName,
      provenance.releaseHead,
      provenance.tag
    );
    if (!options.skipBump && !provenance.tag.exists) {
      if (options.dryRun) {
        console.log(
          `  [dry-run] git tag -a ${tagName} -m "Release ${tagName}" ${provenance.releaseHead}`
        );
      } else {
        const tagObjectId = await dependencies.createTag(
          tagName,
          provenance.releaseHead
        );
        if (!/^[0-9a-f]{40}$/i.test(tagObjectId)) {
          throw new ReleaseError(
            "tag",
            `Tag creation returned an invalid object ID: ${tagObjectId}`
          );
        }
        transaction.markTagCreated(tagName, tagObjectId);
      }
    }
    await yieldToPendingSignals();
    throwIfReleaseAborted(dependencies.signal, currentPhase);
    transaction.commit();
    return { oldVersion, newVersion };
  } catch (cause) {
    const failure = normalizeReleaseFailure(
      cause,
      currentPhase,
      dependencies.signal
    );
    let rollback: ReturnType<ReleaseTransaction["rollback"]>;
    try {
      rollback = transaction.rollback(dependencies.deleteTag);
    } catch (rollbackCause) {
      const rollbackFailure = rollbackCause instanceof Error
        ? rollbackCause
        : new Error(String(rollbackCause));
      const rollbackError = new ReleaseError(
        "rollback",
        `Release failed during ${failure.phase}; rollback failed: ` +
          rollbackFailure.message,
        {
          cause: new AggregateError(
            [failure, rollbackFailure],
            "Release and rollback both failed"
          ),
        }
      );
      rollbackError.recovery = {
        failedPhase: failure.phase,
        oldVersion,
        newVersion,
        restored: [],
        conflicts: [],
        restoreFailures: [],
        tagCleanupFailure: null,
        notarizationRecovery: failure.notarizationRecovery,
        terminationUncertain: failure.terminationUncertain,
        rollbackFailure: rollbackFailure.message,
        retryCommand: formatReleaseRetryCommand(options),
      };
      throw rollbackError;
    }
    if (
      rollback.conflicts.length ||
      rollback.restoreFailures.length ||
      rollback.tagCleanupFailure
    ) {
      const rollbackError = new ReleaseError(
        "rollback",
        `Rollback incomplete after release failed during ${failure.phase}.`,
        { cause: failure }
      );
      rollbackError.recovery = {
        failedPhase: failure.phase,
        oldVersion,
        newVersion,
        restored: rollback.restored,
        conflicts: rollback.conflicts,
        restoreFailures: rollback.restoreFailures,
        tagCleanupFailure: rollback.tagCleanupFailure,
        notarizationRecovery: failure.notarizationRecovery,
        terminationUncertain: failure.terminationUncertain,
        retryCommand: formatReleaseRetryCommand(options),
      };
      throw rollbackError;
    }
    failure.recovery = {
      failedPhase: failure.phase,
      oldVersion,
      newVersion,
      restored: rollback.restored,
      conflicts: [],
      restoreFailures: [],
      tagCleanupFailure: null,
      notarizationRecovery: failure.notarizationRecovery,
      terminationUncertain: failure.terminationUncertain,
      retryCommand: formatReleaseRetryCommand(options),
    };
    throw failure;
  }
}

export async function runReleaseWithSignals(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies = defaultReleaseDependencies
): Promise<ReleaseResult> {
  const controller = new AbortController();
  const onSigint = () => {
    controller.abort(new Error("Release interrupted by SIGINT."));
  };
  const onSigterm = () => {
    controller.abort(new Error("Release interrupted by SIGTERM."));
  };
  process.once("SIGINT", onSigint);
  process.once("SIGTERM", onSigterm);
  try {
    const signal = dependencies.signal
      ? AbortSignal.any([controller.signal, dependencies.signal])
      : controller.signal;
    return await runRelease(options, {
      ...dependencies,
      signal,
    });
  } finally {
    process.off("SIGINT", onSigint);
    process.off("SIGTERM", onSigterm);
  }
}

export function formatReleaseFailure(failure: ReleaseError): string {
  const recovery = failure.recovery;
  if (!recovery) {
    return `Release failed during ${failure.phase}: ${failure.message}`;
  }
  let releaseCause: unknown = failure;
  if (failure.phase === "rollback") {
    if (failure.cause instanceof ReleaseError) {
      releaseCause = failure.cause;
    } else if (failure.cause instanceof AggregateError) {
      releaseCause = failure.cause.errors.find(
        (cause) => cause instanceof ReleaseError
      ) || failure;
    }
  }
  const releaseMessage = releaseCause instanceof Error
    ? releaseCause.message
    : String(releaseCause);
  const lines = [
    `Release failed during ${recovery.failedPhase}: ${releaseMessage}`,
  ];
  const terminationUncertain = recovery.terminationUncertain;
  const metadataIncomplete = !!(
    recovery.rollbackFailure ||
    recovery.conflicts.length ||
    recovery.restoreFailures.length
  );
  const rollbackIncomplete = metadataIncomplete || !!recovery.tagCleanupFailure;
  const retryCommand = recovery.retryCommand || "npm run release";
  if (recovery.rollbackFailure) {
    lines.push(
      "Automatic rollback failed unexpectedly; canonical release metadata " +
        "may not be fully restored: " +
        `rollback failed: ${recovery.rollbackFailure}`
    );
  }
  if (metadataIncomplete) {
    lines.push("Canonical release metadata was not fully restored.");
    if (recovery.restored.length > 0) {
      lines.push("Successfully restored paths:");
      lines.push(...recovery.restored.map((filePath) => `  - ${filePath}`));
    }
  }
  if (recovery.conflicts.length > 0) {
    lines.push("Rollback conflicts:");
    lines.push(...recovery.conflicts.map((filePath) => `  - ${filePath}`));
  }
  if (recovery.restoreFailures.length > 0) {
    lines.push("File restore failures:");
    lines.push(...recovery.restoreFailures.map(
      (restoreFailure) =>
        `  - Failed to restore ${restoreFailure.path}: ` +
        restoreFailure.message
    ));
  }
  if (recovery.tagCleanupFailure) {
    lines.push(
      `Tag cleanup failed for ${recovery.tagCleanupFailure.tagName}: ` +
        recovery.tagCleanupFailure.message
    );
    lines.push(
      `Manually inspect release tag ${recovery.tagCleanupFailure.tagName} ` +
      "before retrying."
    );
  }
  if (rollbackIncomplete && recovery.notarizationRecovery) {
    lines.push(
      "Inspect every listed file and tag before attempting manual recovery."
    );
  }
  if (!metadataIncomplete) {
    if (terminationUncertain) {
      lines.push(
        "Canonical metadata rollback completed, but the old process may " +
          "write again; the restored version at rollback time was " +
          `${recovery.oldVersion}.`
      );
    } else {
      lines.push(
        `Restored canonical release metadata to ${recovery.oldVersion}.`
      );
    }
  }
  lines.push(
    "Any generated files in dist/ and any Apple notarization or R2 side " +
      "effects were left in place and were not rolled back."
  );
  if (terminationUncertain) {
    lines.push(
      "The active release subprocess or process group may still be running " +
        `(PID ${terminationUncertain.processId}, ` +
        `PGID ${terminationUncertain.processGroupId}).`
    );
    lines.push(`Termination detail: ${terminationUncertain.detail}`);
    lines.push(
      "Confirm and terminate the old release process group before retrying."
    );
    return lines.join("\n");
  }
  if (recovery.notarizationRecovery && !rollbackIncomplete) {
    lines.push(recovery.notarizationRecovery.detail);
    lines.push("Recovery command (safe after the clean rollback above):");
    lines.push(recovery.notarizationRecovery.command);
    return lines.join("\n");
  }
  if (rollbackIncomplete) {
    if (retryCommand === "npm run release") {
      lines.push(
        "After resolving the rollback problem, a normal npm run release " +
          `retries candidate ${recovery.newVersion}.`
      );
    } else {
      lines.push(
        `After resolving the rollback problem, retry candidate ${recovery.newVersion} with:`
      );
      lines.push(retryCommand);
    }
  } else {
    if (retryCommand === "npm run release") {
      lines.push(
        `Run npm run release normally to retry candidate ${recovery.newVersion}.`
      );
    } else {
      lines.push(`Retry candidate ${recovery.newVersion} with:`);
      lines.push(retryCommand);
    }
  }
  return lines.join("\n");
}

function shellQuoteReleaseArgument(value: string): string {
  if (/^[A-Za-z0-9_./:@%+=,-]+$/.test(value)) return value;
  return `'${value.replaceAll("'", "'\\''")}'`;
}

export function formatReleaseRetryCommand(
  options: ReleaseOptions,
  overrides: {resumeFromStaple?: boolean} = {}
): string {
  const args: string[] = [];
  if (options.bump && options.bump !== "patch") {
    args.push("--bump", options.bump);
  }
  if (options.bucket) args.push("--bucket", options.bucket);
  if (options.prefix) args.push("--prefix", options.prefix);
  if (options.skipUpload) args.push("--skip-upload");
  if (options.skipBuild) args.push("--skip-build");
  if (options.forceImport === false) args.push("--no-force-import");
  if (options.skipBump) args.push("--skip-bump");
  if (overrides.resumeFromStaple ?? options.resumeFromStaple) {
    args.push("--resume-from-staple");
  }
  if (options.dryRun) args.push("--dry-run");
  return args.length > 0
    ? "npm run release -- " + args.map(shellQuoteReleaseArgument).join(" ")
    : "npm run release";
}

function printReleaseSuccess(newVersion: string): void {
  success(`Release ${newVersion} ready.`);
  log("Next manual steps (not done by this script):");
  log("  - Commit + push (one-liner):");
  log(
    `      git add . && git commit -m "chore: dump to version v${newVersion}" && git push --follow-tags`
  );
  log("  - Deploy the website so dao.msgbyte.com/appcast.xml updates.");
  log("  - (Optional) Create a GitHub Release with the .dmg from dist/.");
}

export function importReleaseSources(
  context: ReleasePhaseContext,
  runner: ReleaseCommandRunner = runStreaming
): Promise<void> {
  const forceImport = context.options.forceImport !== false;
  const args = ["tsx", "scripts/cli.ts", "import"];
  if (forceImport) args.push("--force");
  return runReleaseStep(
    "import",
    context.options.dryRun,
    forceImport ? "Importing patches (force)" : "Importing patches",
    "npx",
    args,
    runner,
    context.signal
  );
}

export function buildReleaseApplication(
  context: ReleasePhaseContext,
  runner: ReleaseCommandRunner = runStreaming
): Promise<void> {
  return runReleaseStep(
    "build",
    context.options.dryRun,
    "Building (release)",
    "npx",
    ["tsx", "scripts/cli.ts", "build"],
    runner,
    context.signal
  );
}

export function packageReleaseArtifact(
  context: ReleasePhaseContext,
  runner: ReleaseCommandRunner = runStreaming
): Promise<void> {
  return runReleaseStep(
    "package",
    context.options.dryRun,
    "Packaging (sign only — notarize handled separately)",
    "npx",
    ["tsx", "scripts/cli.ts", "package", "--sign-id"],
    runner,
    context.signal
  );
}

const defaultReleaseDependencies: ReleaseDependencies = {
  rootDir: ROOT_DIR,
  env: process.env,
  now: () => new Date(),
  head: () => readGitHead(ROOT_DIR) || "",
  tagState: (tagName) => inspectReleaseTag(ROOT_DIR, tagName),
  createTag: (tagName, commit) => createReleaseTag(ROOT_DIR, tagName, commit),
  deleteTag: (tagName, expectedObjectId) =>
    deleteReleaseTag(ROOT_DIR, tagName, expectedObjectId),
  runPhase: async (phase, context) => {
    switch (phase) {
      case "import":
        await importReleaseSources(context);
        return;
      case "build":
        await buildReleaseApplication(context);
        return;
      case "package":
        await packageReleaseArtifact(context);
        return;
      case "notarize":
        if (!context.options.dryRun) {
          await notarizeOrGuide(
            context.dmgPath,
            path.basename(context.dmgPath),
            context.options,
            context.signal
          );
        } else {
          log("Submitting to Apple notary service");
          console.log(
            `  [dry-run] xcrun notarytool submit ${context.dmgPath} ` +
              "--keychain-profile $DAO_NOTARIZE_KEYCHAIN_PROFILE --wait"
          );
        }
        return;
      case "staple":
        if (context.options.resumeFromStaple) {
          log("Resuming from staple (verifying dmg has notarization ticket)");
          if (!context.options.dryRun) {
            await assertStapled(
              context.dmgPath,
              context.options,
              context.signal
            );
          }
        } else if (!context.options.dryRun) {
          await stapleOrGuide(
            context.dmgPath,
            path.basename(context.dmgPath),
            context.options,
            context.signal
          );
        } else {
          log("Stapling notarization ticket");
          console.log(`  [dry-run] xcrun stapler staple ${context.dmgPath}`);
        }
        return;
      case "appcast":
        await generateReleaseAppcast(context, defaultReleaseDependencies);
        return;
      case "upload":
        await uploadReleaseArtifacts(context, defaultReleaseDependencies);
        return;
      case "metadata":
      case "tag":
        return;
      default:
        throw new ReleaseError(
          phase,
          "Unsupported release phase: " + phase
        );
    }
  },
};

export async function runReleaseStep(
  phase: ReleasePhase,
  dryRun: boolean | undefined,
  description: string,
  cmd: string,
  args: string[],
  runner: ReleaseCommandRunner = runStreaming,
  signal?: AbortSignal
): Promise<void> {
  log(description);
  if (dryRun) {
    console.log(`  [dry-run] ${cmd} ${args.join(" ")}`);
    return;
  }
  const code = await runner(cmd, args, { signal });
  if (code !== 0) {
    throw new ReleaseError(
      phase,
      `Step failed (${description}): ${cmd} exited with code ${code}`
    );
  }
}

function bumpVersion(
  current: string,
  kind: "patch" | "minor" | "major"
): string {
  const m = current.match(/^(\d+)\.(\d+)\.(\d+)(.*)$/);
  if (!m) {
    throw new ReleaseError(
      "version",
      `Cannot parse version "${current}" — expected MAJOR.MINOR.PATCH form.`
    );
  }
  let major = parseInt(m[1], 10);
  let minor = parseInt(m[2], 10);
  let patch = parseInt(m[3], 10);
  if (kind === "major") {
    major += 1;
    minor = 0;
    patch = 0;
  } else if (kind === "minor") {
    minor += 1;
    patch = 0;
  } else {
    patch += 1;
  }
  return `${major}.${minor}.${patch}`;
}

function writeDaoVersionContents(
  snapshot: Buffer,
  newVersion: string
): Buffer {
  const raw = snapshot.toString("utf-8");
  // Preserve formatting by doing a targeted regex replacement on the
  // version.display field rather than reserializing JSON.
  const updated = raw.replace(
    /("display"\s*:\s*")[^"]+(")/,
    `$1${newVersion}$2`
  );
  if (updated === raw) {
    throw new ReleaseError(
      "version",
      "Failed to update version.display in dao.json"
    );
  }
  return Buffer.from(updated);
}

interface InfoJsonUpdate {
  version: string;
  chromiumVersion: string;
  releasedAt: string;
}

// Mutate website/public/info.json in place: bump version + releasedAt + the
// version segment inside each platform URL. The host stays the same (it's
// a separate concern — switching CDN host shouldn't be coupled to a version
// bump). Done as targeted string substitutions so we preserve key ordering,
// the leading $schema description, and any future fields we don't recognize.
export function updateInfoJson(filePath: string, update: InfoJsonUpdate): void {
  if (!existsSync(filePath)) {
    throw new ReleaseError("metadata", `info.json not found: ${filePath}`);
  }
  const updated = updateInfoJsonContents(readFileSync(filePath), update);
  writeFileSync(filePath, updated);
  success(
    `info.json → version ${update.version}, releasedAt ${update.releasedAt}`
  );
}

function updateInfoJsonContents(
  snapshot: Buffer,
  update: InfoJsonUpdate
): Buffer {
  const raw = snapshot.toString("utf-8");
  const parsed = JSON.parse(raw) as {
    version: string;
    chromiumVersion?: string;
    releasedAt?: string;
    platforms?: Record<string, { url?: string; label?: string }>;
  };
  const oldVersion = parsed.version;
  if (!oldVersion) {
    throw new ReleaseError("metadata", "info.json has no `version` field");
  }

  let next = raw;
  next = replaceField(next, "version", update.version);
  next = replaceField(next, "chromiumVersion", update.chromiumVersion);
  next = replaceField(next, "releasedAt", update.releasedAt);

  // Replace the old version inside platform URLs only — don't blanket-replace,
  // since the same number could legitimately appear elsewhere later.
  if (parsed.platforms) {
    for (const platform of Object.values(parsed.platforms)) {
      const url = platform?.url;
      if (!url || !url.includes(oldVersion)) continue;
      const updatedUrl = url.split(oldVersion).join(update.version);
      next = next.replace(jsonStringLiteral(url), jsonStringLiteral(updatedUrl));
    }
  }

  if (next === raw) {
    warn("info.json unchanged (already up-to-date?)");
    return Buffer.from(snapshot);
  }
  return Buffer.from(next);
}

function replaceField(raw: string, key: string, value: string): string {
  const re = new RegExp(`("${key}"\\s*:\\s*")[^"]*(")`);
  return raw.replace(re, `$1${value}$2`);
}

// Render a string the way it would appear inside JSON (with surrounding quotes
// and \-escaping) so the URL replacement matches the exact source bytes.
function jsonStringLiteral(s: string): string {
  return JSON.stringify(s);
}

function formatReleaseDate(date: Date): string {
  const yyyy = date.getFullYear();
  const mm = String(date.getMonth() + 1).padStart(2, "0");
  const dd = String(date.getDate()).padStart(2, "0");
  return `${yyyy}-${mm}-${dd}`;
}

// ---------------------------------------------------------------------------
// Stamp the build's git commit into the freshly generated appcast <item>
// ---------------------------------------------------------------------------

function readGitHead(rootDir: string): string | null {
  const r = spawnSync("git", ["rev-parse", "HEAD"], {
    cwd: rootDir,
    encoding: "utf-8",
  });
  if (r.status !== 0) return null;
  const hash = (r.stdout || "").trim();
  return /^[0-9a-f]{40}$/i.test(hash) ? hash : null;
}

export interface ReleaseTagState {
  exists: boolean;
  commit?: string;
  objectId?: string;
}

export function inspectReleaseTag(
  rootDir: string,
  tagName: string
): ReleaseTagState {
  const object = spawnSync(
    "git",
    ["rev-parse", "--verify", `refs/tags/${tagName}`],
    { cwd: rootDir, encoding: "utf-8" }
  );
  if (object.status !== 0) return { exists: false };
  const peeled = spawnSync(
    "git",
    ["rev-parse", "--verify", `refs/tags/${tagName}^{}`],
    { cwd: rootDir, encoding: "utf-8" }
  );
  if (peeled.status !== 0) return { exists: false };
  return {
    exists: true,
    commit: (peeled.stdout || "").trim(),
    objectId: (object.stdout || "").trim(),
  };
}

function createReleaseTag(
  rootDir: string,
  tagName: string,
  commit: string
): string {
  const identity = spawnSync("git", ["var", "GIT_COMMITTER_IDENT"], {
    cwd: rootDir,
    encoding: "utf-8",
  });
  if (identity.status !== 0) {
    throw new ReleaseError(
      "tag",
      `git var GIT_COMMITTER_IDENT failed (exit ${identity.status}):\n` +
        (identity.stderr || identity.stdout || "").trim()
    );
  }
  const tagContents = [
    `object ${commit}`,
    "type commit",
    `tag ${tagName}`,
    `tagger ${(identity.stdout || "").trim()}`,
    "",
    `Release ${tagName}`,
    "",
  ].join("\n");
  const object = spawnSync("git", ["mktag"], {
    cwd: rootDir,
    encoding: "utf-8",
    input: tagContents,
  });
  const objectId = (object.stdout || "").trim();
  if (object.status !== 0 || !/^[0-9a-f]{40}$/i.test(objectId)) {
    throw new ReleaseError(
      "tag",
      `git mktag failed (exit ${object.status}):\n` +
        (object.stderr || object.stdout || "").trim()
    );
  }
  const created = spawnSync(
    "git",
    [
      "update-ref",
      `refs/tags/${tagName}`,
      objectId,
      "0".repeat(40),
    ],
    { cwd: rootDir, encoding: "utf-8" }
  );
  if (created.status !== 0) {
    throw new ReleaseError(
      "tag",
      `git update-ref failed (exit ${created.status}):\n` +
        (created.stderr || created.stdout || "").trim()
    );
  }
  success(`Tagged ${commit.slice(0, 7)} as ${tagName}`);
  return objectId;
}

export function releaseTagDeleteArguments(
  tagName: string,
  expectedObjectId: string
): string[] {
  return [
    "update-ref",
    "-d",
    `refs/tags/${tagName}`,
    expectedObjectId,
  ];
}

function deleteReleaseTag(
  rootDir: string,
  tagName: string,
  expectedObjectId: string
): void {
  const deleted = spawnSync(
    "git",
    releaseTagDeleteArguments(tagName, expectedObjectId),
    {
    cwd: rootDir,
    encoding: "utf-8",
    }
  );
  if (deleted.status !== 0) {
    throw new ReleaseError(
      "rollback",
      `git update-ref -d failed (exit ${deleted.status}):\n` +
        (deleted.stderr || deleted.stdout || "").trim()
    );
  }
}

// Find the <item> whose <enclosure url="..."> ends with dmgName and add
// (or replace) a <dao:gitCommit> child element holding the commit hash.
// Also ensures xmlns:dao is declared on the root <rss> element.
//
// We avoid pulling in an XML parser dependency — the file is generated by
// Sparkle and follows a stable shape, so anchored string surgery is enough.
export function injectGitCommitIntoAppcast(
  appcastPath: string,
  dmgName: string,
  gitCommit: string
): void {
  if (!existsSync(appcastPath)) {
    warn(`appcast not found at ${appcastPath} — skipping git commit stamp.`);
    return;
  }
  const original = readFileSync(appcastPath, "utf-8");
  const DAO_NS = "https://dao.msgbyte.com/xml-namespaces/dao";

  let updated = original;

  // 1. Ensure xmlns:dao is declared on <rss ...>. Sparkle re-emits this
  //    root tag verbatim on every regen, so a one-time injection sticks.
  if (!/\sxmlns:dao\s*=/.test(updated)) {
    updated = updated.replace(
      /<rss\b([^>]*?)>/,
      (_match, attrs: string) => `<rss${attrs} xmlns:dao="${DAO_NS}">`
    );
  }

  // 2. Locate the <item> block whose <enclosure url=...> matches dmgName.
  //    generate_appcast emits the enclosure with the dmg file at the end
  //    of the URL path, so a suffix match on the filename is the most
  //    reliable identifier (independent of download-url-prefix config).
  const itemRegex = /<item\b[^>]*>[\s\S]*?<\/item>/g;
  let matchedRange: { start: number; end: number; body: string } | null = null;
  for (const m of updated.matchAll(itemRegex)) {
    const body = m[0];
    const enclosure = body.match(/<enclosure\b[^>]*\burl="([^"]+)"/);
    if (!enclosure) continue;
    const url = enclosure[1];
    const tail = url.split("/").pop() || "";
    if (tail === dmgName) {
      matchedRange = {
        start: m.index ?? 0,
        end: (m.index ?? 0) + body.length,
        body,
      };
      break;
    }
  }
  if (!matchedRange) {
    warn(
      `Could not find appcast <item> for ${dmgName}; skipping git commit stamp.`
    );
    return;
  }

  // 3. Replace an existing <dao:gitCommit>...</dao:gitCommit> if present,
  //    otherwise insert one right before </item>.
  const tag = `<dao:gitCommit>${gitCommit}</dao:gitCommit>`;
  let newBody: string;
  if (/<dao:gitCommit>[^<]*<\/dao:gitCommit>/.test(matchedRange.body)) {
    newBody = matchedRange.body.replace(
      /<dao:gitCommit>[^<]*<\/dao:gitCommit>/,
      tag
    );
  } else {
    newBody = matchedRange.body.replace(/(\s*)<\/item>\s*$/, (m, indent) => {
      // Match Sparkle's 12-space indent for item children when we can.
      const childIndent = indent && indent.includes("\n")
        ? indent + "    "
        : "\n            ";
      return `${childIndent}${tag}${indent || "\n        "}</item>`;
    });
  }

  updated =
    updated.slice(0, matchedRange.start) +
    newBody +
    updated.slice(matchedRange.end);

  if (updated === original) {
    warn("appcast unchanged after git commit stamp (already up-to-date?).");
    return;
  }
  writeFileSync(appcastPath, updated);
  success(`appcast stamped with git commit ${gitCommit.slice(0, 7)}`);
}

// Parse the just-regenerated appcast and return the set of .delta basenames
// it references in any <enclosure url="...">. Used by Step 6 to whitelist
// delta uploads so stale orphan files left in dist/ from a previous run
// aren't shipped to R2 alongside the current release.
//
// Same string-surgery rationale as injectGitCommitIntoAppcast — Sparkle's
// output shape is stable enough that a regex over <enclosure> tags is
// sufficient without taking on an XML parser dependency.
export function collectReferencedDeltaBasenames(appcastPath: string): Set<string> {
  const referenced = new Set<string>();
  if (!existsSync(appcastPath)) return referenced;
  const xml = readFileSync(appcastPath, "utf-8");
  for (const m of xml.matchAll(/<enclosure\b[^>]*\burl="([^"]+)"/g)) {
    const url = m[1];
    const tail = url.split("/").pop() || "";
    if (tail.endsWith(".delta")) {
      referenced.add(tail);
    }
  }
  return referenced;
}

export function collectCandidateDeltaBasenames(
  appcastPath: string,
  dmgName: string
): Set<string> {
  const referenced = new Set<string>();
  if (!existsSync(appcastPath)) return referenced;
  const xml = readFileSync(appcastPath, "utf-8");
  for (const item of xml.matchAll(/<item\b[^>]*>[\s\S]*?<\/item>/g)) {
    const body = item[0];
    const urls = Array.from(
      body.matchAll(/<enclosure\b[^>]*\burl="([^"]+)"/g),
      (match) => match[1]
    );
    const belongsToCandidate = urls.some((url) => {
      return (url.split("/").pop() || "") === dmgName;
    });
    if (!belongsToCandidate) continue;
    for (const url of urls) {
      const basename = url.split("/").pop() || "";
      if (basename.endsWith(".delta")) referenced.add(basename);
    }
    break;
  }
  return referenced;
}

export async function generateReleaseAppcast(
  context: ReleasePhaseContext,
  dependencies: ReleaseDependencies,
  runner: ReleaseCommandRunner = runStreaming
): Promise<void> {
  const rootDir = dependencies.rootDir;
  const publicAppcast = path.join(rootDir, "website/public/appcast.xml");
  const appcastTemplate = path.join(rootDir, "branding/appcast.template.xml");
  const distDir = path.join(rootDir, "dist");

  if (existsSync(publicAppcast)) {
    log(
      `Seeding dist/appcast.xml from ${path.relative(rootDir, publicAppcast)}`
    );
    if (!context.options.dryRun) {
      copyFileSync(publicAppcast, context.appcastPath);
    }
  } else if (existsSync(appcastTemplate)) {
    warn(
      `${path.relative(rootDir, publicAppcast)} not found — ` +
        "seeding from template (first release?)."
    );
    if (!context.options.dryRun) {
      copyFileSync(appcastTemplate, context.appcastPath);
    }
  } else {
    throw new ReleaseError(
      "appcast",
      "Cannot seed dist/appcast.xml — neither\n" +
        `  ${path.relative(rootDir, publicAppcast)}\n` +
        "nor\n" +
        `  ${path.relative(rootDir, appcastTemplate)}\n` +
        "exists. Restore one of them and rerun."
    );
  }

  if (!context.options.dryRun && !existsSync(context.dmgPath)) {
    throw new ReleaseError(
      "appcast",
      `Expected artifact not found: ${path.relative(rootDir, context.dmgPath)}\n` +
        "package:release should have produced this. Inspect dist/ and rerun."
    );
  }

  const generateAppcast = path.join(
    rootDir,
    "third_party",
    "sparkle",
    "bin",
    "generate_appcast"
  );
  await runReleaseStep(
    "appcast",
    context.options.dryRun,
    "Generating Sparkle appcast",
    generateAppcast,
    [
      "--download-url-prefix",
      "https://dao-release.msgbyte.com/",
      "--maximum-versions",
      "0",
      distDir,
    ],
    runner,
    context.signal
  );

  log("Stamping git commit into the new appcast <item>");
  if (!context.options.dryRun) {
    injectGitCommitIntoAppcast(
      context.appcastPath,
      path.basename(context.dmgPath),
      context.releaseHead
    );
  }
}

export async function uploadReleaseArtifacts(
  context: ReleasePhaseContext,
  dependencies: ReleaseDependencies,
  runner: ReleaseCommandRunner = runStreaming
): Promise<void> {
  const distDir = path.join(dependencies.rootDir, "dist");
  const referencedDeltas = !context.options.dryRun
    ? collectCandidateDeltaBasenames(
      context.appcastPath,
      path.basename(context.dmgPath)
    )
    : new Set<string>();
  const availableDeltas = existsSync(distDir)
    ? readdirSync(distDir).filter((name) => name.endsWith(".delta"))
    : [];
  const deltaPaths = Array.from(referencedDeltas)
    .filter((name) => availableDeltas.includes(name))
    .sort()
    .map((name) => path.join(distDir, name));
  const skippedUnreferenced: string[] = [];
  for (const name of availableDeltas) {
    if (!referencedDeltas.has(name)) {
      skippedUnreferenced.push(name);
    }
  }
  if (skippedUnreferenced.length > 0) {
    warn(
      `Skipping ${skippedUnreferenced.length} unreferenced delta file(s) ` +
        "in dist/ (not present in the current candidate appcast item): " +
        skippedUnreferenced.sort().join(", ")
    );
  }

  const uploadArgs = [
    "tsx",
    "scripts/cli.ts",
    "upload",
    context.dmgPath,
    ...deltaPaths,
  ];
  if (context.options.bucket) {
    uploadArgs.push("--bucket", context.options.bucket);
  }
  if (context.options.prefix) {
    uploadArgs.push("--prefix", context.options.prefix);
  }
  const deltaSummary =
    deltaPaths.length > 0
      ? ` + ${deltaPaths.length} current-candidate delta(s)`
      : " (no current-candidate delta files to upload)";
  await runReleaseStep(
    "upload",
    context.options.dryRun,
    `Uploading ${path.basename(context.dmgPath)}${deltaSummary} to R2`,
    "npx",
    uploadArgs,
    runner,
    context.signal
  );
}

// ---------------------------------------------------------------------------
// Notarize + staple, with a useful recovery path on keychain failures
// ---------------------------------------------------------------------------

interface SpawnInheritResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

// Run a command, streaming stdout+stderr live to the user AND capturing them
// so we can scan for known failure patterns (e.g. notarytool's keychain bug).
function spawnInheritCapture(
  cmd: string,
  args: string[],
  signal?: AbortSignal
): Promise<SpawnInheritResult> {
  console.log(chalk.dim(`$ ${cmd} ${args.join(" ")}`));
  if (signal?.aborted) {
    return Promise.reject(createAbortError(signal.reason));
  }
  const spawnOptions: SpawnOptions = {
    stdio: ["ignore", "pipe", "pipe"],
  };
  if (signal && process.platform !== "win32") {
    spawnOptions.detached = true;
  }
  const child = spawn(cmd, args, spawnOptions);
  let stdout = "";
  let stderr = "";
  const onStdout = (chunk: Buffer) => {
    const s = chunk.toString();
    stdout += s;
    process.stdout.write(s);
  };
  const onStderr = (chunk: Buffer) => {
    const s = chunk.toString();
    stderr += s;
    process.stderr.write(s);
  };
  child.stdout?.on("data", onStdout);
  child.stderr?.on("data", onStderr);
  const removePipeListeners = () => {
    child.stdout?.off("data", onStdout);
    child.stderr?.off("data", onStderr);
  };
  const abandonPipes = () => {
    removePipeListeners();
    for (const stream of [child.stdout, child.stderr]) {
      if (!stream) continue;
      stream.destroy();
      (stream as typeof stream & { unref?: () => void }).unref?.();
    }
  };
  return waitForSpawnedProcess(child, signal, {
    onAbandon: abandonPipes,
  }).then((result) => {
    return {
      code: result.code,
      stdout,
      stderr: result.error ? stderr + String(result.error) : stderr,
    };
  }).finally(removePipeListeners);
}

async function notarizeOrGuide(
  dmgPath: string,
  dmgName: string,
  options: ReleaseOptions,
  signal?: AbortSignal
): Promise<void> {
  log(`Submitting ${dmgName} to Apple notary service ...`);
  const profile = process.env.DAO_NOTARIZE_KEYCHAIN_PROFILE;
  const authArgs = profile
    ? ["--keychain-profile", profile]
    : [
        "--apple-id",
        process.env.DAO_NOTARIZE_APPLE_ID || "",
        "--team-id",
        process.env.DAO_NOTARIZE_TEAM_ID || "",
        "--password",
        process.env.DAO_NOTARIZE_PASSWORD || "",
      ];

  const result = await spawnInheritCapture("xcrun", [
    "notarytool",
    "submit",
    dmgPath,
    ...authArgs,
    "--wait",
  ], signal);

  throwIfReleaseAborted(signal, "notarize");

  // notarytool exits 0 on success AND on Rejected/Invalid; only the
  // final "status:" line tells the truth.
  if (result.code === 0) {
    const finalStatus = (
      result.stdout.match(/status:\s*(\w+)\s*$/im) || []
    )[1];
    if (finalStatus && finalStatus.toLowerCase() === "accepted") {
      success("Notarization accepted");
      return;
    }
    const submissionId = (
      result.stdout.match(/id:\s*([0-9a-f-]{8,})/i) || []
    )[1];
    throw new ReleaseError(
      "notarize",
      `Notarization finished with status: ${finalStatus || "<unknown>"}.\n` +
        (submissionId
          ? `Inspect the failure log via:\n  xcrun notarytool log ${submissionId} --keychain-profile ${profile || "<your-profile>"}`
          : "")
    );
  }

  const failure = new ReleaseError(
    "notarize",
    `notarytool exited with code ${result.code}.`
  );
  const profileName = profile || "<your-profile>";
  const knownKeychainBug = /No Keychain password item found/i.test(
    result.stderr
  );
  failure.notarizationRecovery = {
    command: formatNotarizeRecoveryCommand(
      path.relative(ROOT_DIR, dmgPath),
      profileName,
      options
    ),
    detail: knownKeychainBug
      ? "notarytool reported the known keychain-profile lookup failure."
      : `Manual notarization recovery is available for ${dmgName}.`,
  };
  throw failure;
}

async function stapleOrGuide(
  dmgPath: string,
  dmgName: string,
  options: ReleaseOptions,
  signal?: AbortSignal
): Promise<void> {
  log(`Stapling notarization ticket onto ${dmgName} ...`);
  const result = await spawnInheritCapture("xcrun", [
    "stapler",
    "staple",
    dmgPath,
  ], signal);
  throwIfReleaseAborted(signal, "staple");
  if (result.code === 0) {
    success("Stapled");
    return;
  }
  const failure = new ReleaseError(
    "staple",
    `stapler exited with code ${result.code}.`
  );
  const relativeDmgPath = path.relative(ROOT_DIR, dmgPath);
  failure.notarizationRecovery = {
    command: [
      `xcrun stapler staple ${relativeDmgPath} \\`,
      "  && " + formatReleaseRetryCommand(options, {
        resumeFromStaple: true,
      }),
    ].join("\n"),
    detail: "Notarization succeeded, but stapling needs manual recovery.",
  };
  throw failure;
}

// Verify the dmg has a stapled notarization ticket. Used by --resume-from-staple
// to fail fast if the operator forgot to actually run `xcrun stapler staple`
// before resuming.
async function assertStapled(
  dmgPath: string,
  options: ReleaseOptions,
  signal?: AbortSignal
): Promise<void> {
  const result = await spawnInheritCapture("xcrun", [
    "stapler",
    "validate",
    dmgPath,
  ], signal);
  throwIfReleaseAborted(signal, "staple");
  if (result.code === 0) {
    success("dmg has a valid notarization ticket");
    return;
  }
  const failure = new ReleaseError(
    "staple",
    "--resume-from-staple was passed but the dmg is NOT stapled."
  );
  failure.notarizationRecovery = {
    command: formatNotarizeRecoveryCommand(
      path.relative(ROOT_DIR, dmgPath),
      process.env.DAO_NOTARIZE_KEYCHAIN_PROFILE || "<your-profile>",
      options
    ),
    detail: "The retained dmg must be notarized and stapled before resuming.",
  };
  throw failure;
}

export function formatNotarizeRecoveryCommand(
  dmgPath: string,
  profile: string,
  options: ReleaseOptions = {}
): string {
  return [
    "xcrun notarytool submit " + dmgPath +
      " --keychain-profile " + profile + " --wait \\",
    "  && xcrun stapler staple " + dmgPath + " \\",
    "  && " + formatReleaseRetryCommand(options, {resumeFromStaple: true}),
  ].join("\n");
}
