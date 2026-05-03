import {
  PRODUCT_VERSION,
  CHROMIUM_VERSION,
  DOWNLOAD_URL_MAC_ARM64,
  GITHUB_URL,
} from '@/lib/version';
import { Button } from './ui/Button';
import { LucideIcon } from './ui/LucideIcon';
import { BrowserFrame } from './ui/BrowserFrame';
import { BrowserFrameMockup } from './ui/BrowserFrameMockup';
import styles from './Hero.module.css';

export function Hero() {
  return (
    <section id="top" className={styles.hero}>
      <div className={styles.eyebrow}>DAO BROWSER</div>
      <h1 className={styles.h1}>An opinionated browser, built on Chromium.</h1>
      <p className={styles.subtitle}>Vertical tabs, soft corners, content first.</p>
      <div className={styles.ctas}>
        <Button href={DOWNLOAD_URL_MAC_ARM64} variant="primary" download>
          <LucideIcon name="download" size={16} aria-hidden />
          Download for Mac (Apple Silicon)
        </Button>
        <Button href={GITHUB_URL} variant="ghost" external>
          <LucideIcon name="star" size={16} aria-hidden />
          Star on GitHub
        </Button>
      </div>
      <p className={styles.versionHint}>
        Latest: v{PRODUCT_VERSION} · Chromium {CHROMIUM_VERSION}
      </p>
      <div className={styles.mockupWrap}>
        <BrowserFrame>
          <BrowserFrameMockup variant="sidebar" />
        </BrowserFrame>
      </div>
    </section>
  );
}
