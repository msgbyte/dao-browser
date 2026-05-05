import styles from './KeyboardVisual.module.css';

interface Shortcut {
  keys: string[];
  label: string;
}

// Mirrors the shortcuts taught in the Dao Welcome page
// (src/dao/browser/ui/webui/resources/welcome/dao_welcome_app.ts).
const SHORTCUTS: Shortcut[] = [
  { keys: ['⌘', 'T'], label: 'New tab' },
  { keys: ['⌘', 'W'], label: 'Close tab' },
  { keys: ['⌘', 'L'], label: 'Command bar' },
  { keys: ['⌘', 'S'], label: 'Toggle sidebar' },
  { keys: ['⌘', 'E'], label: 'Toggle AI Agent' },
  { keys: ['⌘', '⇧', 'C'], label: 'Copy page URL' },
];

export function KeyboardVisual() {
  return (
    <div className={styles.root} aria-hidden="true">
      <ul className={styles.list}>
        {SHORTCUTS.map((s) => (
          <li key={s.label} className={styles.item}>
            <div className={styles.keys}>
              {s.keys.map((k, i) => (
                <kbd key={i} className={styles.key}>
                  {k}
                </kbd>
              ))}
            </div>
            <span className={styles.label}>{s.label}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}
