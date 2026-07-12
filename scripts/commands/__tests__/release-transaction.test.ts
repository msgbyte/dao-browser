import {
  existsSync,
  mkdtempSync,
  readdirSync,
  readFileSync,
  renameSync,
  unlinkSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {describe, expect, it, vi} from 'vitest';
import {ReleaseTransaction} from '../release-transaction.js';

function fixture() {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-transaction-'));
  const dao = path.join(root, 'dao.json');
  const appcast = path.join(root, 'appcast.xml');
  writeFileSync(dao, '{"display":"1.0.70"}\n');
  return {dao, appcast};
}

describe('ReleaseTransaction', () => {
  it('restores exact bytes and removes a file created by the release', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao, files.appcast]);
    transaction.writeFile(files.dao, Buffer.from('{"display":"1.0.71"}\n'));
    transaction.writeFile(files.appcast, Buffer.from('<rss>1.0.71</rss>\n'));

    expect(transaction.rollback(vi.fn())).toEqual({
      restored: [files.dao, files.appcast],
      conflicts: [],
      restoreFailures: [],
      tagCleanupFailure: null,
    });
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.70"}\n');
    expect(existsSync(files.appcast)).toBe(false);
  });

  it('preserves and reports a concurrent edit', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.writeFile(files.dao, Buffer.from('{"display":"1.0.71"}\n'));
    writeFileSync(files.dao, '{"display":"user-edit"}\n');

    expect(transaction.rollback(vi.fn())).toEqual({
      restored: [],
      conflicts: [files.dao],
      restoreFailures: [],
      tagCleanupFailure: null,
    });
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"user-edit"}\n');
  });

  it('deletes only a tag recorded as created by the transaction', () => {
    const files = fixture();
    const deleteTag = vi.fn();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.markTagCreated('v1.0.71', '1'.repeat(40));
    transaction.rollback(deleteTag);
    expect(deleteTag).toHaveBeenCalledWith('v1.0.71', '1'.repeat(40));
  });

  it('keeps a replacement tag when exact-object CAS cleanup fails', () => {
    const files = fixture();
    const createdObjectId = '1'.repeat(40);
    const replacementObjectId = '2'.repeat(40);
    let currentObjectId: string | null = replacementObjectId;
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.markTagCreated('v1.0.71', createdObjectId);

    const result = transaction.rollback((tagName, expectedObjectId) => {
      expect(tagName).toBe('v1.0.71');
      if (currentObjectId !== expectedObjectId) {
        throw new Error('tag object changed');
      }
      currentObjectId = null;
    });

    expect(result.tagCleanupFailure).toMatchObject({
      tagName: 'v1.0.71',
      expectedObjectId: createdObjectId,
      message: 'tag object changed',
    });
    expect(currentObjectId).toBe(replacementObjectId);
  });

  it('leaves the managed file untouched after a partial temp write fails', () => {
    const files = fixture();
    const root = path.dirname(files.dao);
    const transaction = new ReleaseTransaction([files.dao], {
      writeTemporaryFile: (tempPath, contents) => {
        writeFileSync(tempPath, contents.subarray(0, 5));
        throw new Error('partial temp write failed');
      },
      replaceFile: renameSync,
      removeFile: unlinkSync,
    });

    expect(() => transaction.writeFile(
      files.dao,
      Buffer.from('{"display":"1.0.71"}\n'),
    )).toThrow('partial temp write failed');
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.70"}\n');
    expect(readdirSync(root).sort()).toEqual(['dao.json']);
  });

  it('preserves a concurrent save that lands while the temp file is written',
     () => {
       const files = fixture();
       const root = path.dirname(files.dao);
       const transaction = new ReleaseTransaction([files.dao], {
         writeTemporaryFile: (tempPath, contents) => {
           writeFileSync(tempPath, contents);
           writeFileSync(files.dao, '{"display":"user-edit"}\n');
         },
         replaceFile: renameSync,
         removeFile: unlinkSync,
       });

       expect(() => transaction.writeFile(
         files.dao,
         Buffer.from('{"display":"1.0.71"}\n'),
       )).toThrow('Managed release file changed concurrently');
       expect(readFileSync(files.dao, 'utf-8'))
         .toBe('{"display":"user-edit"}\n');
       expect(readdirSync(root).sort()).toEqual(['dao.json']);
  });

  it('keeps restored progress when another file restore fails', () => {
    const files = fixture();
    const info = path.join(path.dirname(files.dao), 'info.json');
    writeFileSync(info, '{"version":"1.0.70"}\n');
    const transaction = new ReleaseTransaction([files.dao, info], {
      writeTemporaryFile: (tempPath, contents) => {
        if (
          path.basename(tempPath).includes('dao.json') &&
          contents.includes(Buffer.from('1.0.70'))
        ) {
          throw new Error('restore dao failed');
        }
        writeFileSync(tempPath, contents);
      },
      replaceFile: renameSync,
      removeFile: (filePath) => unlinkSync(filePath),
    });
    transaction.writeFile(files.dao, Buffer.from('{"display":"1.0.71"}\n'));
    transaction.writeFile(info, Buffer.from('{"version":"1.0.71"}\n'));

    const result = transaction.rollback(vi.fn());

    expect(result.restored).toEqual([info]);
    expect(result.conflicts).toEqual([]);
    expect(result.restoreFailures).toHaveLength(1);
    expect(result.restoreFailures[0]).toMatchObject({
      path: files.dao,
      message: 'restore dao failed',
    });
    expect(readFileSync(info, 'utf-8')).toBe('{"version":"1.0.70"}\n');
  });

  it('reports tag cleanup failure without losing restored files', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.writeFile(files.dao, Buffer.from('{"display":"1.0.71"}\n'));
    transaction.markTagCreated('v1.0.71', '1'.repeat(40));

    const result = transaction.rollback(() => {
      throw new Error('delete tag failed');
    });

    expect(result.restored).toEqual([files.dao]);
    expect(result.restoreFailures).toEqual([]);
    expect(result.tagCleanupFailure).toMatchObject({
      tagName: 'v1.0.71',
      expectedObjectId: '1'.repeat(40),
      message: 'delete tag failed',
    });
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.70"}\n');
  });

  it('commit keeps transaction-owned contents', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.writeFile(files.dao, Buffer.from('{"display":"1.0.71"}\n'));
    transaction.commit();
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.71"}\n');
    expect(() => transaction.rollback(vi.fn())).toThrow(
      'Release transaction has already been committed.');
  });
});
