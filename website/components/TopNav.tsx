'use client';

/* eslint-disable @next/next/no-img-element */
import Link from 'next/link';
import { GITHUB_URL } from '@/lib/version';
import { Button } from './ui/Button';
import { LucideIcon } from './ui/LucideIcon';
import { GitHubIcon } from './ui/BrandIcons';
import { trackEvent } from '@/lib/analytics';
import styles from './TopNav.module.css';

export function TopNav() {
  return (
    <nav className={styles.nav} aria-label="Primary">
      <a href="#top" className={styles.brand} aria-label="Dao Browser, back to top">
        {/*
          Two logos, one shown per color scheme. Uses CSS @media in TopNav.module.css.
          - Light logo (`dao_logo.svg`) ships as a 256×256 PNG (`dao-logo-light.png`).
          - Dark logo (`dao-logo.svg`) is the dark-mode product mark.
        */}
        <img
          src="/dao-logo-light.png"
          alt=""
          width={28}
          height={28}
          className={`${styles.brandLogo} ${styles.brandLogoLight}`}
        />
        <img
          src="/dao-logo.svg"
          alt=""
          width={28}
          height={28}
          className={`${styles.brandLogo} ${styles.brandLogoDark}`}
        />
        <span className={styles.brandWord}>Dao</span>
      </a>
      <div className={styles.links}>
        <Link
          href="/#features"
          className={styles.link}
          onClick={() => trackEvent('nav_link_click', { target: 'features' })}
        >
          <span className={styles.linkText}>Features</span>
        </Link>
        <Link
          href="/agent"
          className={styles.link}
          onClick={() => trackEvent('nav_link_click', { target: 'agent' })}
        >
          <span className={styles.linkText}>AI Agent</span>
        </Link>
        <a
          href={GITHUB_URL}
          target="_blank"
          rel="noopener noreferrer"
          className={styles.link}
          aria-label="GitHub repository"
          onClick={() => trackEvent('github_click', { source: 'nav' })}
        >
          <GitHubIcon size={18} aria-hidden />
        </a>
        <Button
          href="/download"
          variant="primary"
          onClick={() => trackEvent('download_click', { source: 'nav' })}
        >
          <LucideIcon name="download" size={16} aria-hidden />
          Download
        </Button>
      </div>
    </nav>
  );
}
