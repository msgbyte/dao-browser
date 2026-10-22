import {execFile} from 'node:child_process';
import path from 'node:path';
import {clearScreenDown, cursorTo, emitKeypressEvents, moveCursor, type Key} from 'node:readline';
import {promisify} from 'node:util';
import {Command} from 'commander';
import {ROOT_DIR, error, log, success} from '../utils.js';
import {ReleaseTransaction} from './release-transaction.js';
import {readWindowsUpdateFeed, verifyPublishedWindowsUpdate, WINDOWS_UPDATE_ORIGIN,
  windowsUpdateManifestName} from '../windows-update.js';

const exec = promisify(execFile);
const INFO_FILE = 'website/public/info.json';
const FEED_FILE = 'website/public/windows-update.json';

interface RollbackOptions {
  version?: string;
  dryRun?: boolean;
}

interface WebsiteRelease {
  version: string;
  releasedAt: string;
  contents: string;
  urls: string[];
  windowsVersion?: string;
}

async function git(root: string, ...args: string[]): Promise<string> {
  try {
    return (await exec('git', args, {cwd: root, maxBuffer: 4 * 1024 * 1024})).stdout;
  } catch (cause) {
    const failure = cause as Error & {stderr?: string};
    throw new Error(`git ${args[0]} failed: ${failure.stderr?.trim() || failure.message}`);
  }
}

function parseRelease(contents: string): WebsiteRelease {
  const info = JSON.parse(contents);
  if (!info || typeof info.version !== 'string' || !/^\d+\.\d+\.\d+$/.test(info.version) ||
      typeof info.chromiumVersion !== 'string' || !/^\d+\.\d+\.\d+\.\d+$/.test(info.chromiumVersion) ||
      typeof info.releasedAt !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(info.releasedAt) ||
      !info.platforms || typeof info.platforms !== 'object' || Array.isArray(info.platforms) ||
      typeof info.default !== 'string' || !Object.hasOwn(info.platforms, info.default)) {
    throw new Error(`Invalid website release metadata in ${INFO_FILE}.`);
  }
  const urls = Object.values(info.platforms).map(platform => {
    const url = (platform as {url?: unknown} | null)?.url;
    if (typeof url !== 'string' || new URL(url).protocol !== 'https:') {
      throw new Error('Website installers must have HTTPS download URLs.');
    }
    return url;
  });
  const windowsVersion = info.platforms.win?.url?.match(/\/dao-browser-(\d+\.\d+\.\d+)-windows-x64\.exe$/)?.[1];
  if (info.platforms.win?.version && info.platforms.win.version !== windowsVersion) {
    throw new Error('Windows website version does not match its installer URL.');
  }
  return {version: info.version, releasedAt: info.releasedAt, contents, urls, windowsVersion};
}

export async function listWebsiteReleases(root = ROOT_DIR): Promise<WebsiteRelease[]> {
  const commits = (await git(root, 'log', '--format=%H', '--', INFO_FILE)).trim().split('\n').filter(Boolean);
  const releases = new Map<string, WebsiteRelease>();
  for (const commit of commits) {
    // Release tags can precede their website metadata commit.
    const contents = await git(root, 'show', `${commit}:${INFO_FILE}`);
    let release: WebsiteRelease;
    try { release = parseRelease(contents); }
    catch { continue; } // Older website schemas are not rollback candidates.
    if (!releases.has(release.version)) releases.set(release.version, release);
  }
  if (!releases.size) throw new Error(`No usable website releases in Git history. Use a full checkout with ${INFO_FILE} history.`);
  return [...releases.values()];
}

export async function resolveRollbackVersion(
  releases: WebsiteRelease[], version?: string,
  input: NodeJS.ReadStream = process.stdin, output: NodeJS.WriteStream = process.stdout,
): Promise<WebsiteRelease> {
  if (version !== undefined) {
    const release = releases.find(candidate => candidate.version === version);
    if (!release) throw new Error(`Website release ${version} was not found in local Git history.`);
    return release;
  }
  if (!input.isTTY || !output.isTTY) throw new Error('Pass --version when not running in an interactive terminal.');
  const wasRaw = input.isRaw;
  const wasFlowing = input.readableFlowing === true;
  return new Promise((resolve, reject) => {
    let selected = 0;
    let rows = 0;
    const render = () => {
      if (rows) moveCursor(output, 0, -rows);
      cursorTo(output, 0);
      clearScreenDown(output);
      const start = Math.max(0, Math.min(selected - 3, releases.length - 7));
      const visible = releases.slice(start, start + 7);
      for (const [index, release] of visible.entries()) {
        output.write(`${start + index === selected ? '>' : ' '} ${release.version} (${release.releasedAt})\n`);
      }
      rows = visible.length;
    };
    const finish = (cause?: Error) => {
      input.off('keypress', onKey);
      input.off('end', cancel);
      input.off('error', finish);
      input.setRawMode(wasRaw);
      if (!wasFlowing) input.pause();
      output.write('\n');
      if (cause) reject(cause); else resolve(releases[selected]);
    };
    const cancel = () => finish(new Error('Rollback cancelled.'));
    const onKey = (_text: string, key: Key) => {
      if (key.name === 'return' || key.name === 'enter') finish();
      else if (key.name === 'escape' || (key.ctrl && (key.name === 'c' || key.name === 'd'))) cancel();
      else if (key.name === 'up' || key.name === 'down') {
        selected = (selected + (key.name === 'up' ? -1 : 1) + releases.length) % releases.length;
        render();
      }
    };
    emitKeypressEvents(input);
    input.on('keypress', onKey);
    input.once('end', cancel);
    input.once('error', finish);
    input.setRawMode(true);
    input.resume();
    output.write('Select a website release (Up/Down, Enter to select, Esc to cancel):\n');
    render();
  });
}

export async function runReleaseRollback(options: RollbackOptions, root = ROOT_DIR): Promise<void> {
  const file = path.join(root, INFO_FILE);
  const feedFile = path.join(root, FEED_FILE);
  const transaction = new ReleaseTransaction([file, feedFile]);
  const original = transaction.originalFileContents(file);
  if (!original) throw new Error(`Missing ${INFO_FILE}.`);
  const current = parseRelease(original.toString('utf8'));
  const releases = await listWebsiteReleases(root);
  log(`Current website download version: ${current.version}`);
  const selected = await resolveRollbackVersion(releases, options.version);
  if (!selected.windowsVersion) throw new Error('This website release has no supported Windows installer. Select a version with a signed Windows update.');
  log(`${INFO_FILE}: ${current.version} -> ${selected.version} (${selected.releasedAt})`);
  for (const url of selected.urls) log(url);
  log(`${FEED_FILE}: restore the original signed Windows ${selected.windowsVersion} manifest from R2.`);
  log('Website downloads and the Windows feed change. Application versions and Mac appcast stay unchanged.');
  log(`Commit ${INFO_FILE} and ${FEED_FILE} locally. Push and deployment remain manual.`);
  if (options.dryRun) return;

  const head = (await git(root, 'rev-parse', 'HEAD')).trim();
  const assertCleanMain = async () => {
    if ((await git(root, 'branch', '--show-current')).trim() !== 'main' ||
        (await git(root, 'status', '--porcelain')).trim() ||
        (await git(root, 'rev-parse', 'HEAD')).trim() !== head) {
      throw new Error('Run website rollback from a clean, unchanged main checkout. Finish existing work first.');
    }
  };
  await assertCleanMain();
  for (const url of selected.urls) {
    try {
      const response = await fetch(url, {method: 'HEAD', cache: 'no-store', signal: AbortSignal.timeout(15000)});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
    } catch (cause) {
      throw new Error(`Cannot verify installer ${url}: ${cause instanceof Error ? cause.message : String(cause)}`);
    }
  }
  const envelope = await readWindowsUpdateFeed(`${WINDOWS_UPDATE_ORIGIN}/${windowsUpdateManifestName(selected.windowsVersion)}`);
  if (!envelope) throw new Error(`No archived signed Windows manifest for ${selected.windowsVersion}. Nothing was changed.`);
  const windows = await verifyPublishedWindowsUpdate(envelope);
  if (windows.version !== selected.windowsVersion) throw new Error('Archived Windows manifest version does not match the selected download.');
  const contents = new Map([[INFO_FILE, selected.contents], [FEED_FILE, envelope]]);
  const changed = [...contents.keys()].filter(name =>
    transaction.originalFileContents(path.join(root, name))?.toString('utf8') !== contents.get(name));
  if (!changed.length) {
    log('The selected website downloads and Windows feed are already current.');
    return;
  }
  await assertCleanMain();
  try {
    transaction.writeFile(file, Buffer.from(selected.contents));
    transaction.writeFile(feedFile, Buffer.from(envelope));
    await git(root, 'commit', '--only', '-m', `chore(release): roll back website release to ${selected.version}`, '--', INFO_FILE, FEED_FILE);
  } catch (cause) {
    const rollback = transaction.rollback(() => {});
    const unresolved = [...rollback.conflicts, ...rollback.restoreFailures.map(failure => failure.path)];
    throw new Error(`${cause}\n` + (unresolved.length
      ? `Inspect unrestored files: ${unresolved.join(', ')}` : 'Website metadata was restored.'));
  }
  transaction.commit();
  const commit = (await git(root, 'rev-parse', 'HEAD')).trim();
  let matching = true;
  for (const [name, expected] of contents) {
    if (await git(root, 'show', `${commit}:${name}`) !== expected) matching = false;
  }
  if ((await git(root, 'show', '-s', '--format=%P', commit)).trim() !== head ||
      (await git(root, 'diff-tree', '--no-commit-id', '--name-only', '-r', commit)).trim() !== changed.sort().join('\n') || !matching) {
    throw new Error('Inspect the local commit: it contains unexpected changes. Nothing was pushed.');
  }
  success(`Committed website download version ${selected.version} locally (${commit.slice(0, 7)}). Nothing was pushed.`);
}

export const releaseRollbackCommand = new Command('release-rollback')
  .description('Select historical website downloads and the signed Windows feed, then commit locally')
  .option('--version <version>', 'Website release version (prompt when omitted)')
  .option('--dry-run', 'Preview the selection without changes or network access')
  .action(async (options: RollbackOptions) => {
    try { await runReleaseRollback(options); }
    catch (cause) { error(cause instanceof Error ? cause.message : String(cause)); process.exitCode = 1; }
  });
