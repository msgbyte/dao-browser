import { GITHUB_URL, DOWNLOAD_URL_MAC_ARM64 } from '@/lib/version';
import styles from './Footer.module.css';

export function Footer() {
  const releasesUrl = `${GITHUB_URL}/releases`;
  return (
    <footer className={styles.footer}>
      <div className={styles.inner}>
        <div className={styles.brand}>
          <span className={styles.brandName}>Dao</span>
          <p className={styles.tagline}>
            An opinionated browser. Built on Chromium. Open source.
          </p>
        </div>

        <div>
          <h3 className={styles.colTitle}>Product</h3>
          <ul className={styles.list}>
            <li>
              <a className={styles.link} href="#features">
                Features
              </a>
            </li>
            <li>
              <a className={styles.link} href={DOWNLOAD_URL_MAC_ARM64} download>
                Download
              </a>
            </li>
            <li>
              <a
                className={styles.link}
                href={GITHUB_URL}
                target="_blank"
                rel="noopener noreferrer"
              >
                GitHub
              </a>
            </li>
            <li>
              <a
                className={styles.link}
                href={releasesUrl}
                target="_blank"
                rel="noopener noreferrer"
              >
                Releases
              </a>
            </li>
          </ul>
        </div>

        <div>
          <h3 className={styles.colTitle}>Resources</h3>
          <ul className={styles.list}>
            <li>
              <a
                className={styles.link}
                href={`${GITHUB_URL}/blob/main/README.md`}
                target="_blank"
                rel="noopener noreferrer"
              >
                Docs
              </a>
            </li>
            <li>
              <a
                className={styles.link}
                href={`${GITHUB_URL}/blob/main/DESIGN.md`}
                target="_blank"
                rel="noopener noreferrer"
              >
                Design System
              </a>
            </li>
            <li>
              <a
                className={styles.link}
                href={GITHUB_URL}
                target="_blank"
                rel="noopener noreferrer"
              >
                Source
              </a>
            </li>
          </ul>
        </div>

        <div>
          <h3 className={styles.colTitle}>Acknowledgements</h3>
          <ul className={styles.list}>
            <li>
              <a
                className={styles.link}
                href="https://arc.net"
                target="_blank"
                rel="noopener noreferrer"
              >
                Inspired by Arc
              </a>
            </li>
            <li>
              <a
                className={styles.link}
                href="https://www.chromium.org"
                target="_blank"
                rel="noopener noreferrer"
              >
                Built on Chromium
              </a>
            </li>
          </ul>
        </div>
      </div>

      <div className={styles.bottom}>
        <span>© 2026 Dao Browser</span>
        <span>Built with Next.js · Hosted on Cloudflare Pages</span>
      </div>
    </footer>
  );
}
