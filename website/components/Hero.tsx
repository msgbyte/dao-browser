import { PRODUCT_VERSION, CHROMIUM_VERSION, GITHUB_URL } from '@/lib/version';
import { TrackedButton } from './ui/TrackedButton';
import { LucideIcon, type IconName } from './ui/LucideIcon';
import { HeroAnimation } from './ui/HeroAnimation';
import styles from './Hero.module.css';

const HIGHLIGHTS: { icon: IconName; label: string; hint?: string }[] = [
  { icon: 'panel-left', label: 'Vertical Tabs' },
  { icon: 'command', label: 'Command Bar', hint: '⌘L' },
  { icon: 'sparkles', label: 'AI Agent', hint: '⌘E' },
  { icon: 'moon', label: 'Force Dark' },
];

export function Hero() {
  return (
    <section id="top" className={styles.hero}>
      <div className={`aurora ${styles.aurora}`} aria-hidden>
        <i className="a1" />
        <i className="a2" />
        <i className="a3" />
      </div>
      <div className={styles.inner}>
        <div className={styles.eyebrow}>DAO BROWSER</div>
        <h1 className={styles.h1}>
          An opinionated browser,{' '}
          <span className="grad-text">built on Chromium.</span>
        </h1>
        <p className={styles.subtitle}>
          Vertical tabs, soft corners, content first. A real Chromium build —
          not a wrapper.
        </p>
        <div className={styles.ctas}>
          <TrackedButton
            href="/download"
            variant="primary"
            event="download_click"
            eventPayload={{ source: 'hero' }}
          >
            <LucideIcon name="download" size={16} aria-hidden />
            Download
          </TrackedButton>
          <TrackedButton
            href={GITHUB_URL}
            variant="ghost"
            external
            event="github_click"
            eventPayload={{ source: 'hero' }}
          >
            <LucideIcon name="star" size={16} aria-hidden />
            Star on GitHub
          </TrackedButton>
        </div>
        <p className={styles.versionHint}>
          Latest: v{PRODUCT_VERSION} · Chromium {CHROMIUM_VERSION}
        </p>

        <ul className={styles.highlights} aria-label="Built-in highlights">
          {HIGHLIGHTS.map((item) => (
            <li key={item.label} className={styles.highlight}>
              <LucideIcon
                name={item.icon}
                size={16}
                className={styles.highlightIcon}
                aria-hidden
              />
              <span className={styles.highlightLabel}>{item.label}</span>
              {item.hint ? (
                <kbd className={styles.highlightHint}>{item.hint}</kbd>
              ) : null}
            </li>
          ))}
        </ul>
      </div>

      <div className={styles.mockupWrap}>
        <HeroAnimation />
      </div>
    </section>
  );
}
