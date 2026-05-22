import {
  PRODUCT_VERSION,
  CHROMIUM_VERSION,
  GITHUB_URL,
} from '@/lib/version';
import { Button } from './ui/Button';
import { LucideIcon } from './ui/LucideIcon';
import styles from './DownloadCTA.module.css';

export function DownloadCTA() {
  return (
    <section id="download" className={styles.section}>
      <h2 className={styles.heading}>Try Dao Browser.</h2>
      <p className={styles.sub}>
        v{PRODUCT_VERSION} · Built on Chromium {CHROMIUM_VERSION}
      </p>
      <Button href="/download" variant="primary">
        <LucideIcon name="download" size={16} aria-hidden />
        Download
      </Button>
      <p className={styles.hint}>
        macOS (Apple Silicon) available · Linux and Windows coming soon ·{' '}
        <a
          href={GITHUB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className={styles.hintLink}
        >
          ★ Star on GitHub for updates
        </a>
      </p>
    </section>
  );
}
