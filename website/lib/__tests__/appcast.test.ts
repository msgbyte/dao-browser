import {describe, expect, it} from 'vitest';

import {
  formatBytes,
  formatPubDate,
  getHistoryDownloadUrl,
  parseAppcast,
  type AppcastItem,
} from '../appcast.js';

describe('parseAppcast', () => {
  it('ignores commented sample items, skips delta enclosures, and sorts newest first',
     () => {
    const xml = `<?xml version="1.0"?>
<rss>
  <channel>
    <!--
    <item>
      <title>Example only</title>
      <pubDate>Mon, 01 Jan 2024 00:00:00 GMT</pubDate>
      <sparkle:shortVersionString>0.0.1</sparkle:shortVersionString>
      <enclosure url="https://example.com/commented.dmg" length="1" />
    </item>
    -->
    <item>
      <title>Dao 1.0.0</title>
      <pubDate>Mon, 01 Jan 2024 00:00:00 GMT</pubDate>
      <sparkle:shortVersionString>1.0.0</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>
      <dao:gitCommit>aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa</dao:gitCommit>
      <enclosure url="https://cdn.example.com/Dao1.0-0.9.delta"
                 sparkle:deltaFrom="0.9" length="123" />
      <enclosure url="https://cdn.example.com/dao-browser-1.0.0.dmg"
                 length="1048576" />
    </item>
    <item>
      <title>Dao 1.1.0</title>
      <pubDate>Tue, 02 Jan 2024 00:00:00 GMT</pubDate>
      <sparkle:shortVersionString>1.1.0</sparkle:shortVersionString>
      <enclosure url="https://cdn.example.com/dao-browser-1.1.0.dmg"
                 length="2097152" />
    </item>
  </channel>
</rss>`;

    const items = parseAppcast(xml);

    expect(items.map(item => item.shortVersion)).toEqual(['1.1.0', '1.0.0']);
    expect(items[0]).toMatchObject({
      title: 'Dao 1.1.0',
      downloadUrl: 'https://cdn.example.com/dao-browser-1.1.0.dmg',
      sizeBytes: 2097152,
      gitCommit: null,
      minimumSystemVersion: null,
    });
    expect(items[1]).toMatchObject({
      downloadUrl: 'https://cdn.example.com/dao-browser-1.0.0.dmg',
      gitCommit: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
      minimumSystemVersion: '14.0',
    });
  });

  it('falls back to numeric version sorting when pubDate is not parseable', () => {
    const xml = `<rss><channel>
      <item>
        <title>2.0.0</title>
        <pubDate>not a date</pubDate>
        <enclosure url="https://cdn.example.com/2.dmg" length="1" />
      </item>
      <item>
        <title>10.0.0</title>
        <pubDate>also not a date</pubDate>
        <enclosure url="https://cdn.example.com/10.dmg" length="1" />
      </item>
    </channel></rss>`;

    expect(parseAppcast(xml).map(item => item.shortVersion))
        .toEqual(['10.0.0', '2.0.0']);
  });
});

describe('appcast formatting helpers', () => {
  it('formats sizes and preserves invalid dates as-is', () => {
    expect(formatBytes(0)).toBe('—');
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(1536)).toBe('1.5 KB');
    expect(formatBytes(1048576)).toBe('1.0 MB');
    expect(formatPubDate('not a date')).toBe('not a date');
  });
});

describe('version history download URLs', () => {
  it('keeps the latest release on R2 and sends older releases to GitHub', () => {
    const latest: AppcastItem = {
      title: '1.0.101.0',
      shortVersion: '1.0.101.0',
      pubDate: 'Thu, 03 Sep 2026 05:20:24 +0800',
      downloadUrl:
        'https://dao-release.msgbyte.com/dao-browser-1.0.101-mac-arm64.dmg',
      sizeBytes: 158940030,
      gitCommit: null,
      minimumSystemVersion: '12.0',
    };
    const previous = {
      ...latest,
      title: '1.0.100.0',
      shortVersion: '1.0.100.0',
      downloadUrl:
        'https://dao-release.msgbyte.com/dao-browser-1.0.100-mac-arm64.dmg',
    };

    expect(getHistoryDownloadUrl(
      latest,
      true,
      'https://github.com/msgbyte/dao-browser',
    )).toBe(latest.downloadUrl);
    expect(getHistoryDownloadUrl(
      previous,
      false,
      'https://github.com/msgbyte/dao-browser',
    )).toBe(
      'https://github.com/msgbyte/dao-browser/releases/download/v1.0.100/' +
        'dao-browser-1.0.100-mac-arm64.dmg',
    );
  });
});
