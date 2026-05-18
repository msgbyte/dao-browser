import type { ReactNode } from 'react';
import { LucideIcon, type IconName } from './ui/LucideIcon';
import { BrowserFrame } from './ui/BrowserFrame';
import { BrowserFrameMockup, type MockupVariant } from './ui/BrowserFrameMockup';
import styles from './FeatureSection.module.css';

export interface Bullet {
  icon: IconName;
  label: string;
}

export interface FeatureSectionProps {
  id?: string;
  eyebrow: string;
  heading: string;
  body: string;
  bullets: Bullet[];
  /**
   * Either a built-in BrowserFrame mockup variant, or a custom React node
   * (e.g. a keyboard visualization) rendered in place of the BrowserFrame.
   */
  mockupVariant?: MockupVariant;
  customVisual?: ReactNode;
  mockupSide?: 'left' | 'right'; // default 'right'
  /** Optional "Learn more →" link rendered under the bullets. */
  learnMoreHref?: string;
  learnMoreLabel?: string;
}

export function FeatureSection({
  id,
  eyebrow,
  heading,
  body,
  bullets,
  mockupVariant,
  customVisual,
  mockupSide = 'right',
  learnMoreHref,
  learnMoreLabel,
}: FeatureSectionProps) {
  return (
    <section
      id={id}
      className={`${styles.section} ${mockupSide === 'left' ? styles.reverse : ''}`}
    >
      <div className={styles.text}>
        <div className={styles.eyebrow}>{eyebrow}</div>
        <h2 className={styles.h2}>{heading}</h2>
        <p className={styles.body}>{body}</p>
        <ul className={styles.bullets}>
          {bullets.map((b) => (
            <li key={b.label} className={styles.bullet}>
              <LucideIcon name={b.icon} size={18} className={styles.bulletIcon} />
              <span>{b.label}</span>
            </li>
          ))}
        </ul>
        {learnMoreHref && (
          <a className={styles.learnMore} href={learnMoreHref}>
            {learnMoreLabel ?? 'Learn more'}
            <LucideIcon name="arrow-right" size={14} />
          </a>
        )}
      </div>
      <div>
        {customVisual ?? (mockupVariant && (
          <BrowserFrame>
            <BrowserFrameMockup variant={mockupVariant} />
          </BrowserFrame>
        ))}
      </div>
    </section>
  );
}
