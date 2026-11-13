import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const root = path.resolve(__dirname, '..', '..');
const cli = path.join(root, 'src/dao/browser/cli');

describe('Home CLI packaged resources', () => {
  it.skipIf(!existsSync(path.join(root, 'engine/src/tools/grit/grit/extern/tclib.py')))('embeds offline English and Chinese help from the actual localized resources', () => {
    const temporary = mkdtempSync(path.join(tmpdir(), 'dao-home-help-test-'));
    try {
      const output = path.join(temporary, 'help.h');
      execFileSync('python3', [path.join(cli, 'embed_home_help.py'),
        path.join(root, 'engine/src/tools/grit'),
        path.join(root, 'src/dao/browser/strings/dao_strings.grd'),
        path.join(root, 'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb'),
        output], { cwd: temporary });
      const embedded = readFileSync(output, 'utf8');
      const english = JSON.parse(embedded.match(/kDaoHomeHelpEn\[\] = (.*);/)![1]);
      const chinese = JSON.parse(embedded.match(/kDaoHomeHelpZh\[\] = (.*);/)![1]);
      expect(english).toContain('dao-browser home <command>');
      expect(english).toContain('--workspace');
      expect(chinese).toContain('临时工作区绝对路径');
      expect(chinese).toContain('dao-browser home guide');
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });

  it('embeds each installed-version guide without changing its bytes', () => {
    const temporary = mkdtempSync(path.join(tmpdir(), 'dao-home-guide-test-'));
    try {
      for (const name of ['dao_home_guide', 'dao_home_runtime_guide', 'dao_home_sources_guide', 'dao_home_entry_skill']) {
        const output = path.join(temporary, `${name}.h`);
        const source = path.join(cli, `${name}.md`);
        execFileSync('python3', [path.join(cli, 'embed_text.py'), source, output, 'kGuide'], { cwd: temporary });
        const embedded = readFileSync(output, 'utf8');
        expect(embedded.slice(embedded.indexOf('R"DAOTEXT(') + 10, embedded.lastIndexOf(')DAOTEXT"')))
          .toBe(readFileSync(source, 'utf8'));
      }
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });
});
