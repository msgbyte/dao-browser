import {existsSync, readFileSync, readdirSync} from 'node:fs';
import path from 'node:path';

const ROOT_MARKDOWN_FILES = ['README.md', 'DESIGN.md', 'AGENTS.md'];

function collectTopLevelMarkdownFiles(directory: string): string[] {
  if (!existsSync(directory)) {
    return [];
  }

  return readdirSync(directory, {withFileTypes: true})
      .filter(entry => entry.isFile() && entry.name.endsWith('.md'))
      .map(entry => path.join(directory, entry.name));
}

function displayPath(root: string, filePath: string): string {
  return path.relative(root, filePath).split(path.sep).join('/');
}

function findDocumentedScripts(content: string): string[] {
  const scripts = new Set<string>();
  const pattern = /npm run ([a-z0-9](?:[a-z0-9:_-]*[a-z0-9_-])?)/gi;
  for (const match of content.matchAll(pattern)) {
    scripts.add(match[1]);
  }
  return [...scripts];
}

function findLocalLinks(content: string): string[] {
  const links: string[] = [];
  const pattern = /!?\[[^\]]*\]\(([^)]+)\)/g;
  for (const match of content.matchAll(pattern)) {
    let target = match[1].trim();
    if (target.startsWith('<') && target.endsWith('>')) {
      target = target.slice(1, -1);
    } else {
      target = target.split(/\s+["']/u, 1)[0];
    }
    if (!target || target.startsWith('#') || target.startsWith('/') ||
        /^[a-z][a-z0-9+.-]*:/iu.test(target)) {
      continue;
    }
    links.push(target.split('#', 1)[0]);
  }
  return links;
}

export function checkDocumentation(root: string): string[] {
  const packageJsonPath = path.join(root, 'package.json');
  if (!existsSync(packageJsonPath)) {
    return ['package.json: required for documentation checks'];
  }

  const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
    scripts?: Record<string, string>;
  };
  const packageScripts = new Set(Object.keys(packageJson.scripts ?? {}));
  const markdownFiles = [
    ...ROOT_MARKDOWN_FILES.map(file => path.join(root, file))
        .filter(existsSync),
    ...collectTopLevelMarkdownFiles(path.join(root, 'docs')),
  ];
  const errors: string[] = [];

  for (const filePath of markdownFiles) {
    const relativePath = displayPath(root, filePath);
    const content = readFileSync(filePath, 'utf-8');

    for (const script of findDocumentedScripts(content)) {
      if (!packageScripts.has(script)) {
        errors.push(`${relativePath}: unknown npm script "${script}"`);
      }
    }

    if (/chrome:\/\/dao-/u.test(content)) {
      errors.push(`${relativePath}: use dao:// URLs for Dao-owned WebUI pages`);
    }
    if (/\bMEMORY\.md\b/u.test(content)) {
      errors.push(`${relativePath}: public documentation must not reference local MEMORY.md`);
    }
    if (/\ball ~\d+ patches\b/u.test(content)) {
      errors.push(`${relativePath}: must not hardcode volatile patch totals`);
    }

    for (const link of findLocalLinks(content)) {
      let decodedLink = link;
      try {
        decodedLink = decodeURIComponent(link);
      } catch {
        // Keep the original target so the resulting error identifies it.
      }
      const targetPath = path.resolve(path.dirname(filePath), decodedLink);
      if (!existsSync(targetPath)) {
        errors.push(`${relativePath}: broken local link "${link}"`);
      }
    }

    if (relativePath === 'docs/features.md' &&
        (/Chromium Core Integration Patches \(\d+ total\)/u.test(content) ||
         /Exactly \*\*\d+ patch files/u.test(content) ||
         /^- \*\*Version\*\*:/mu.test(content) ||
         /^- \*\*Source footprint\*\*:/mu.test(content))) {
      errors.push(
          `${relativePath}: must not hardcode volatile project facts; link to their source instead`,
      );
    }
    if (relativePath === 'docs/features.md' &&
        /Chromium \d+\.\d+\.\d+\.\d+/u.test(content)) {
      errors.push(
          `${relativePath}: must not hardcode the Chromium baseline; link to dao.json instead`,
      );
    }
  }

  return errors;
}

function parseRoot(argv: string[]): string {
  const rootIndex = argv.indexOf('--root');
  if (rootIndex === -1) {
    return process.cwd();
  }
  const root = argv[rootIndex + 1];
  if (!root) {
    throw new Error('--root requires a path');
  }
  return path.resolve(root);
}

const root = parseRoot(process.argv.slice(2));
const errors = checkDocumentation(root);
if (errors.length > 0) {
  for (const error of errors) {
    console.error(`docs-check: ${error}`);
  }
  process.exitCode = 1;
} else {
  console.log('docs-check: documentation checks passed');
}
