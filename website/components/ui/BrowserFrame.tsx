import type { ReactNode } from 'react';
import styles from './BrowserFrame.module.css';

export interface BrowserFrameProps {
  children: ReactNode;
}

export function BrowserFrame({ children }: BrowserFrameProps) {
  return (
    <div className={styles.frame} aria-hidden="true">
      <div className={styles.titlebar}>
        <span className={styles.dot} />
        <span className={styles.dot} />
        <span className={styles.dot} />
      </div>
      <div className={styles.body}>{children}</div>
    </div>
  );
}
