import type { Metadata } from 'next';
import { TopNav } from '@/components/TopNav';
import { Footer } from '@/components/Footer';
import { LucideIcon } from '@/components/ui/LucideIcon';
import { GITHUB_URL } from '@/lib/version';
import {
  loadAppcastItems,
  formatBytes,
  formatPubDate,
  type AppcastItem,
} from '@/lib/appcast';
import {
  buildWebPageJsonLd,
  createMetadata,
  jsonLdScriptProps,
} from '@/lib/seo';
import styles from './history.module.css';

const HISTORY_TITLE = 'Version History - Dao Browser';
const HISTORY_DESCRIPTION =
  'Every shipped Dao Browser release, with direct download links for the macOS Apple Silicon Chromium browser build.';

export const metadata: Metadata = createMetadata({
  title: HISTORY_TITLE,
  description: HISTORY_DESCRIPTION,
  path: '/history',
  keywords: [
    'Dao Browser download',
    'Dao Browser releases',
    'Chromium browser releases',
    'macOS browser download',
  ],
});

const HISTORY_PAGE_JSON_LD = buildWebPageJsonLd({
  title: HISTORY_TITLE,
  description: HISTORY_DESCRIPTION,
  path: '/history',
  type: 'CollectionPage',
});

// Resolve at request time so a redeployed appcast.xml is picked up
// without rebuilding the page.
export const dynamic = 'force-dynamic';

export default async function HistoryPage() {
  const items = await loadAppcastItems();
  return (
    <>
      <TopNav />
      <main className={styles.root}>
        <header className={styles.header}>
          <div className={styles.eyebrow}>
            <LucideIcon name="package" size={14} aria-hidden />
            Version history
          </div>
          <h1 className={styles.heading}>All releases</h1>
          <p className={styles.subhead}>
            Every Dao Browser build we&apos;ve shipped, newest first. Each link
            points at the signed .dmg on our release CDN.
          </p>
        </header>

        {items.length === 0 ? (
          <div className={styles.empty}>
            No releases found in appcast.xml.
          </div>
        ) : (
          <ol className={styles.list}>
            {items.map((item, idx) => (
              <ReleaseRow key={item.shortVersion} item={item} isLatest={idx === 0} />
            ))}
          </ol>
        )}
      </main>
      <Footer />
      <script {...jsonLdScriptProps(HISTORY_PAGE_JSON_LD)} />
    </>
  );
}

function ReleaseRow({ item, isLatest }: { item: AppcastItem; isLatest: boolean }) {
  const fileName = item.downloadUrl.split('/').pop() ?? 'download.dmg';
  const commitShort = item.gitCommit ? item.gitCommit.slice(0, 7) : null;
  return (
    <li className={`${styles.item} ${isLatest ? styles.itemLatest : ''}`}>
      <div className={styles.meta}>
        <div className={styles.versionRow}>
          <span className={styles.version}>v{item.shortVersion}</span>
          {isLatest && <span className={styles.latestBadge}>Latest</span>}
        </div>
        <div className={styles.detailRow}>
          {item.pubDate && (
            <span className={styles.detail}>
              <LucideIcon name="calendar" size={14} aria-hidden />
              {formatPubDate(item.pubDate)}
            </span>
          )}
          <span className={styles.detail}>
            <LucideIcon name="hard-drive" size={14} aria-hidden />
            {formatBytes(item.sizeBytes)}
          </span>
          {item.minimumSystemVersion && (
            <span className={styles.detail}>
              <LucideIcon name="apple" size={14} aria-hidden />
              macOS {item.minimumSystemVersion}+
            </span>
          )}
          {commitShort && (
            <a
              className={styles.commitLink}
              href={`${GITHUB_URL}/commit/${item.gitCommit}`}
              target="_blank"
              rel="noopener noreferrer"
              title={item.gitCommit ?? undefined}
            >
              {commitShort}
            </a>
          )}
        </div>
      </div>
      <a
        href={item.downloadUrl}
        className={`${styles.downloadBtn} ${isLatest ? styles.downloadBtnPrimary : ''}`}
        download={fileName}
      >
        <LucideIcon name="download" size={16} aria-hidden />
        Download
      </a>
    </li>
  );
}
