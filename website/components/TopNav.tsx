/* eslint-disable @next/next/no-img-element */
import { GITHUB_URL, DOWNLOAD_URL_MAC_ARM64 } from '@/lib/version';
import { Button } from './ui/Button';
import { LucideIcon } from './ui/LucideIcon';
import { GitHubIcon } from './ui/BrandIcons';
import styles from './TopNav.module.css';

export function TopNav() {
  return (
    <nav className={styles.nav} aria-label="Primary">
      <a href="#top" className={styles.brand} aria-label="Dao Browser, back to top">
        <img
          src="/dao-logo.svg"
          alt=""
          width={28}
          height={28}
          className={styles.brandLogo}
        />
        <span className={styles.brandWord}>Dao</span>
      </a>
      <div className={styles.links}>
        <a href="#features" className={styles.link}>
          <span className={styles.linkText}>Features</span>
        </a>
        <a
          href={GITHUB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className={styles.link}
          aria-label="GitHub repository"
        >
          <GitHubIcon size={18} aria-hidden />
        </a>
        <Button href={DOWNLOAD_URL_MAC_ARM64} variant="primary" download>
          <LucideIcon name="download" size={16} aria-hidden />
          Download
        </Button>
      </div>
    </nav>
  );
}
