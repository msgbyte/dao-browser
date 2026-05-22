import {
  PRODUCT_VERSION,
  CHROMIUM_VERSION,
  GITHUB_URL,
} from '@/lib/version';
import { TrackedButton } from './ui/TrackedButton';
import { TrackedLink } from './ui/TrackedLink';
import { LucideIcon } from './ui/LucideIcon';
import styles from './DownloadCTA.module.css';

export function DownloadCTA() {
  return (
    <section id="download" className={styles.section}>
      <h2 className={styles.heading}>Try Dao Browser.</h2>
      <p className={styles.sub}>
        v{PRODUCT_VERSION} · Built on Chromium {CHROMIUM_VERSION}
      </p>
      <TrackedButton
        href="/download"
        variant="primary"
        event="download_click"
        eventPayload={{ source: 'cta' }}
      >
        <LucideIcon name="download" size={16} aria-hidden />
        Download
      </TrackedButton>
      <p className={styles.hint}>
        macOS (Apple Silicon) available · Linux and Windows coming soon ·{' '}
        <TrackedLink
          href={GITHUB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className={styles.hintLink}
          event="github_click"
          eventPayload={{ source: 'cta_hint' }}
        >
          ★ Star on GitHub for updates
        </TrackedLink>
      </p>
    </section>
  );
}
