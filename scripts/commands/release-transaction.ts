import {randomUUID} from 'node:crypto';
import {
  existsSync,
  readFileSync,
  renameSync,
  unlinkSync,
  writeFileSync,
} from 'node:fs';
import path from 'node:path';

interface FileState {
  existed: boolean;
  contents: Buffer | null;
}

export interface ReleaseTransactionFileOperations {
  writeTemporaryFile: (filePath: string, contents: Buffer) => void;
  replaceFile: (temporaryPath: string, targetPath: string) => void;
  removeFile: (filePath: string) => void;
}

const defaultFileOperations: ReleaseTransactionFileOperations = {
  writeTemporaryFile: (filePath, contents) => {
    writeFileSync(filePath, contents, {flag: 'wx'});
  },
  replaceFile: (temporaryPath, targetPath) => {
    renameSync(temporaryPath, targetPath);
  },
  removeFile: (filePath) => unlinkSync(filePath),
};

export interface ReleaseRollbackResult {
  restored: string[];
  conflicts: string[];
  restoreFailures: ReleaseFileRestoreFailure[];
  tagCleanupFailure: ReleaseTagCleanupFailure | null;
}

export interface ReleaseFileRestoreFailure {
  path: string;
  message: string;
  cause: unknown;
}

export interface ReleaseTagCleanupFailure {
  tagName: string;
  expectedObjectId: string;
  message: string;
  cause: unknown;
}

export class ReleaseConcurrentEditError extends Error {
  constructor(readonly filePath: string) {
    super('Managed release file changed concurrently: ' + filePath);
    this.name = 'ReleaseConcurrentEditError';
  }
}

function readState(filePath: string): FileState {
  return existsSync(filePath)
    ? {existed: true, contents: readFileSync(filePath)}
    : {existed: false, contents: null};
}

function cloneState(state: FileState): FileState {
  return {
    existed: state.existed,
    contents: state.contents ? Buffer.from(state.contents) : null,
  };
}

function equal(left: FileState, right: FileState): boolean {
  if (left.existed !== right.existed) return false;
  return !left.existed || left.contents!.equals(right.contents!);
}

export class ReleaseTransaction {
  private readonly original = new Map<string, FileState>();
  private readonly owned = new Map<string, FileState>();
  private createdTag: {tagName: string; objectId: string} | null = null;
  private state: 'active' | 'committed' | 'rolled-back' = 'active';

  constructor(
    paths: string[],
    private readonly fileOperations = defaultFileOperations,
  ) {
    for (const filePath of paths) {
      const state = readState(filePath);
      this.original.set(filePath, state);
      this.owned.set(filePath, cloneState(state));
    }
  }

  originalFileContents(filePath: string): Buffer | null {
    const original = this.managed(filePath, this.original);
    return original.contents ? Buffer.from(original.contents) : null;
  }

  writeFile(filePath: string, contents: Buffer): void {
    this.assertActive();
    const expected = this.managed(filePath, this.owned);
    const target = Buffer.from(contents);
    this.atomicWrite(filePath, target, expected);
    this.owned.set(filePath, {existed: true, contents: target});
  }

  markTagCreated(tagName: string, objectId: string): void {
    this.assertActive();
    this.createdTag = {tagName, objectId};
  }

  commit(): void {
    this.assertActive();
    for (const [filePath, expected] of this.owned) {
      if (!equal(readState(filePath), expected)) {
        throw new ReleaseConcurrentEditError(filePath);
      }
    }
    this.state = 'committed';
  }

  rollback(
    deleteTag: (tagName: string, expectedObjectId: string) => void,
  ): ReleaseRollbackResult {
    if (this.state === 'committed') {
      throw new Error('Release transaction has already been committed.');
    }
    if (this.state === 'rolled-back') {
      throw new Error('Release transaction has already been rolled back.');
    }
    const restored: string[] = [];
    const conflicts: string[] = [];
    const restoreFailures: ReleaseFileRestoreFailure[] = [];
    for (const [filePath, original] of this.original) {
      try {
        const expected = this.managed(filePath, this.owned);
        if (!equal(readState(filePath), expected)) {
          conflicts.push(filePath);
          continue;
        }
        if (equal(original, expected)) continue;
        if (original.existed) {
          this.atomicWrite(filePath, original.contents!, expected);
        } else if (existsSync(filePath)) {
          this.fileOperations.removeFile(filePath);
        }
        restored.push(filePath);
      } catch (cause) {
        if (cause instanceof ReleaseConcurrentEditError) {
          conflicts.push(filePath);
          continue;
        }
        restoreFailures.push({
          path: filePath,
          message: cause instanceof Error ? cause.message : String(cause),
          cause,
        });
      }
    }
    let tagCleanupFailure: ReleaseTagCleanupFailure | null = null;
    if (this.createdTag) {
      try {
        deleteTag(this.createdTag.tagName, this.createdTag.objectId);
      } catch (cause) {
        tagCleanupFailure = {
          tagName: this.createdTag.tagName,
          expectedObjectId: this.createdTag.objectId,
          message: cause instanceof Error ? cause.message : String(cause),
          cause,
        };
      }
    }
    this.state = 'rolled-back';
    return {restored, conflicts, restoreFailures, tagCleanupFailure};
  }

  private atomicWrite(
    filePath: string,
    contents: Buffer,
    expected: FileState,
  ): void {
    const temporaryPath = path.join(
      path.dirname(filePath),
      `.${path.basename(filePath)}.release-${process.pid}-${randomUUID()}.tmp`,
    );
    try {
      this.fileOperations.writeTemporaryFile(temporaryPath, contents);
      if (!equal(readState(filePath), expected)) {
        throw new ReleaseConcurrentEditError(filePath);
      }
      this.fileOperations.replaceFile(temporaryPath, filePath);
    } finally {
      if (existsSync(temporaryPath)) {
        try {
          this.fileOperations.removeFile(temporaryPath);
        } catch {
          // Preserve the primary write error; a leftover temp file is inert.
        }
      }
    }
  }

  private assertActive(): void {
    if (this.state !== 'active') {
      throw new Error('Release transaction is already ' + this.state + '.');
    }
  }

  private managed(filePath: string, states: Map<string, FileState>): FileState {
    const state = states.get(filePath);
    if (!state) throw new Error('Unmanaged release file: ' + filePath);
    return state;
  }
}
