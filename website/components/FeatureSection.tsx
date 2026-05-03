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
  mockupVariant: MockupVariant;
  mockupSide?: 'left' | 'right'; // default 'right'
}

export function FeatureSection({
  id,
  eyebrow,
  heading,
  body,
  bullets,
  mockupVariant,
  mockupSide = 'right',
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
      </div>
      <div>
        <BrowserFrame>
          <BrowserFrameMockup variant={mockupVariant} />
        </BrowserFrame>
      </div>
    </section>
  );
}
