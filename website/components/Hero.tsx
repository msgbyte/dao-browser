import {
  PRODUCT_VERSION,
  CHROMIUM_VERSION,
  GITHUB_URL,
} from '@/lib/version';
import { Button } from './ui/Button';
import { LucideIcon } from './ui/LucideIcon';
import { HeroAnimation } from './ui/HeroAnimation';
import styles from './Hero.module.css';

export function Hero() {
  return (
    <section id="top" className={styles.hero}>
      <div className={styles.eyebrow}>DAO BROWSER</div>
      <h1 className={styles.h1}>An opinionated browser, built on Chromium.</h1>
      <p className={styles.subtitle}>Vertical tabs, soft corners, content first.</p>
      <div className={styles.ctas}>
        <Button variant="primary" disabled>
          <LucideIcon name="clock" size={16} aria-hidden />
          Coming Soon
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
        <HeroAnimation />
      </div>
    </section>
  );
}
