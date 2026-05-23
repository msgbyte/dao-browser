import { readFile } from 'node:fs/promises';
import path from 'node:path';

export interface AppcastItem {
  title: string;
  shortVersion: string;
  pubDate: string;
  downloadUrl: string;
  sizeBytes: number;
  gitCommit: string | null;
  minimumSystemVersion: string | null;
}

const ITEM_RE = /<item\b[^>]*>([\s\S]*?)<\/item>/g;
const TITLE_RE = /<title>([^<]*)<\/title>/;
const PUBDATE_RE = /<pubDate>([^<]*)<\/pubDate>/;
const SHORT_VERSION_RE = /<sparkle:shortVersionString>([^<]*)<\/sparkle:shortVersionString>/;
const MIN_SYS_RE = /<sparkle:minimumSystemVersion>([^<]*)<\/sparkle:minimumSystemVersion>/;
const GIT_COMMIT_RE = /<dao:gitCommit>([^<]*)<\/dao:gitCommit>/;
// Match the top-level <enclosure> (the full dmg), not the ones inside
// <sparkle:deltas>. The full enclosure appears before <sparkle:deltas>
// in generate_appcast's output, so the first match in the body is the
// correct one — but reject deltas explicitly via the sparkle:deltaFrom
// attribute as a safety net.
const ENCLOSURE_RE =
  /<enclosure\b([^>]*?)\/>/g;

function parseEnclosure(body: string): { url: string; length: number } | null {
  ENCLOSURE_RE.lastIndex = 0;
  for (const m of body.matchAll(ENCLOSURE_RE)) {
    const attrs = m[1];
    if (/\bsparkle:deltaFrom\s*=/.test(attrs)) continue;
    const url = attrs.match(/\burl="([^"]+)"/)?.[1];
    const length = attrs.match(/\blength="(\d+)"/)?.[1];
    if (!url) continue;
    return {
      url,
      length: length ? parseInt(length, 10) : 0,
    };
  }
  return null;
}

export function parseAppcast(xml: string): AppcastItem[] {
  const items: AppcastItem[] = [];
  // Strip XML comments first — appcast.xml ships with an example <item>
  // inside a <!-- ... --> block that we must NOT surface as a real release.
  const stripped = xml.replace(/<!--[\s\S]*?-->/g, '');
  for (const m of stripped.matchAll(ITEM_RE)) {
    const body = m[1];
    const enclosure = parseEnclosure(body);
    if (!enclosure) continue;
    const title = body.match(TITLE_RE)?.[1]?.trim() ?? '';
    const pubDate = body.match(PUBDATE_RE)?.[1]?.trim() ?? '';
    const shortVersion =
      body.match(SHORT_VERSION_RE)?.[1]?.trim() || title;
    const minSys = body.match(MIN_SYS_RE)?.[1]?.trim() ?? null;
    const gitCommit = body.match(GIT_COMMIT_RE)?.[1]?.trim() ?? null;
    items.push({
      title,
      shortVersion,
      pubDate,
      downloadUrl: enclosure.url,
      sizeBytes: enclosure.length,
      gitCommit,
      minimumSystemVersion: minSys,
    });
  }
  items.sort((a, b) => {
    const ta = Date.parse(a.pubDate);
    const tb = Date.parse(b.pubDate);
    if (!Number.isNaN(ta) && !Number.isNaN(tb)) return tb - ta;
    return b.shortVersion.localeCompare(a.shortVersion, undefined, {
      numeric: true,
    });
  });
  return items;
}

export async function loadAppcastItems(): Promise<AppcastItem[]> {
  const filePath = path.join(process.cwd(), 'public', 'appcast.xml');
  const xml = await readFile(filePath, 'utf-8');
  return parseAppcast(xml);
}

export function formatBytes(bytes: number): string {
  if (!bytes || bytes <= 0) return '—';
  const units = ['B', 'KB', 'MB', 'GB'];
  let value = bytes;
  let i = 0;
  while (value >= 1024 && i < units.length - 1) {
    value /= 1024;
    i += 1;
  }
  return `${value.toFixed(value >= 100 || i === 0 ? 0 : 1)} ${units[i]}`;
}

export function formatPubDate(input: string): string {
  const t = Date.parse(input);
  if (Number.isNaN(t)) return input;
  return new Date(t).toLocaleDateString('en-US', {
    year: 'numeric',
    month: 'short',
    day: 'numeric',
  });
}
