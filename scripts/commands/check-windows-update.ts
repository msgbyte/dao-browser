import {readFileSync} from 'node:fs';
import path from 'node:path';
import {Command} from 'commander';
import {ROOT_DIR, error, success} from '../utils.js';
import {verifyPublishedWindowsUpdate, WINDOWS_UPDATE_FEED} from '../windows-update.js';

export async function checkWindowsUpdate(root = ROOT_DIR): Promise<void> {
  const envelope = readFileSync(path.join(root, 'website/public', WINDOWS_UPDATE_FEED), 'utf8');
  const payload = await verifyPublishedWindowsUpdate(envelope);
  success(`Website Windows feed ${payload.version} matches its published signed artifact.`);
}

export const checkWindowsUpdateCommand = new Command('check-windows-update')
  .description('Validate the staged Windows feed against its published signed artifact and installer')
  .action(async () => {
    try { await checkWindowsUpdate(); }
    catch (cause) { error(cause instanceof Error ? cause.message : String(cause)); process.exitCode = 1; }
  });
