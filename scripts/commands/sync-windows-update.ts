import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {Command} from 'commander';
import {ROOT_DIR, error, runStreaming, success} from '../utils.js';
import {readWindowsUpdateFeed, verifyPublishedWindowsUpdate, WINDOWS_UPDATE_FEED,
  WINDOWS_UPDATE_ORIGIN, WINDOWS_UPDATE_URL} from '../windows-update.js';

interface SyncOptions {check?: boolean; bucket?: string;}

export async function syncWindowsUpdate(options: SyncOptions, root = ROOT_DIR,
    upload: (args: string[]) => Promise<void> = async args => {
      const code = await runStreaming('npx', ['tsx', 'scripts/cli.ts', 'upload', ...args]);
      if (code !== 0) throw new Error('Legacy Windows feed upload failed. Retry after checking the public feed.');
    }): Promise<void> {
  const envelope = readFileSync(path.join(root, 'website/public', WINDOWS_UPDATE_FEED), 'utf8');
  const payload = await verifyPublishedWindowsUpdate(envelope);
  if (options.check) {
    success(`Website Windows feed ${payload.version} matches its published signed artifact.`);
    return;
  }
  if (await readWindowsUpdateFeed(WINDOWS_UPDATE_URL) !== envelope) {
    throw new Error('The production website has not deployed this Windows feed. Nothing was mirrored.');
  }
  const legacyUrl = `${WINDOWS_UPDATE_ORIGIN}/${WINDOWS_UPDATE_FEED}`;
  const bucket = options.bucket || process.env.R2_BUCKET;
  if (!bucket) throw new Error('Set R2_BUCKET or pass --bucket to mirror the legacy Windows feed.');
  const temporary = mkdtempSync(path.join(os.tmpdir(), 'dao-windows-feed-'));
  try {
    const snapshot = path.join(temporary, WINDOWS_UPDATE_FEED);
    writeFileSync(snapshot, envelope);
    await upload([snapshot, '--bucket', bucket, '--key', WINDOWS_UPDATE_FEED, '--content-type', 'application/json']);
    if (await readWindowsUpdateFeed(legacyUrl) !== envelope) {
      throw new Error('The public legacy feed did not confirm the upload. Verify both feeds before retrying.');
    }
    success(`Legacy Windows feed now matches website version ${payload.version}.`);
  } finally {
    rmSync(temporary, {recursive: true, force: true});
  }
}

export const syncWindowsUpdateCommand = new Command('sync-windows-update')
  .description('Verify the website Windows feed and mirror the deployed version to legacy R2 clients')
  .option('--check', 'Validate staged signed metadata and published artifacts without uploading')
  .option('--bucket <bucket>', 'Legacy R2 bucket (defaults to R2_BUCKET)')
  .action(async (options: SyncOptions) => {
    try { await syncWindowsUpdate(options); }
    catch (cause) { error(cause instanceof Error ? cause.message : String(cause)); process.exitCode = 1; }
  });
