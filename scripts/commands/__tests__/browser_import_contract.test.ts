import fs from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

const repoRoot = path.resolve(import.meta.dirname, '../../..');

function readDaoSource(relativePath: string): string {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

describe('browser import ownership contract', () => {
  it('blocks browser shutdown until snapshot cleanup is complete', () => {
    const header = readDaoSource(
      'src/dao/browser/import/dao_profile_snapshot.h',
    );
    const implementation = readDaoSource(
      'src/dao/browser/import/dao_profile_snapshot.cc',
    );

    expect(header).toContain('base::OnTaskRunnerDeleter');
    expect(implementation).toMatch(
      /CreateSequencedTaskRunner\(\s*\{base::MayBlock\(\),[\s\S]*?base::TaskShutdownBehavior::BLOCK_SHUTDOWN\}/,
    );
  });
});
