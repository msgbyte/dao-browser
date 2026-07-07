import { LucideIcon, type IconName } from './ui/LucideIcon';
import styles from './FeatureGrid.module.css';

interface GridItem {
  icon: IconName;
  title: string;
  body: string;
  highlight?: boolean;
}

const ITEMS: GridItem[] = [
  {
    icon: 'moon',
    title: 'Force Dark Mode',
    body: 'Darken bright sites with Chromium Auto Dark Mode, only when your system is already dark.',
    highlight: true,
  },
  {
    icon: 'picture-in-picture-2',
    title: 'Picture-in-Picture',
    body: 'Pop a video out, keep watching while you read.',
  },
  {
    icon: 'columns-2',
    title: 'Split View',
    body: 'Drag two tabs together, work side by side.',
  },
  {
    icon: 'sliders-horizontal',
    title: 'Control Center',
    body: 'One panel, every browser knob you need.',
  },
  {
    icon: 'square',
    title: 'Little Dao',
    body: 'A miniature Dao window for quick lookups.',
  },
  {
    icon: 'palette',
    title: 'Adaptive Theming',
    body: "The chrome adapts to the page you're reading.",
  },
  {
    icon: 'globe',
    title: 'Native Chromium',
    body: 'Built on real Chromium 147 — not a wrapper.',
  },
];

export function FeatureGrid() {
  return (
    <section className={styles.section}>
      <h2 className={styles.heading}>And there&apos;s more.</h2>
      <ul className={styles.grid} role="list">
        {ITEMS.map((it) => (
          <li key={it.title} className={`${styles.card} ${it.highlight ? styles.highlight : ''}`}>
            <LucideIcon name={it.icon} size={22} className={styles.icon} />
            <h3 className={styles.cardTitle}>{it.title}</h3>
            <p className={styles.cardBody}>{it.body}</p>
          </li>
        ))}
      </ul>
    </section>
  );
}
