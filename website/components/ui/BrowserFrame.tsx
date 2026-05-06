import type { ReactNode } from 'react';
import styles from './BrowserFrame.module.css';

export interface BrowserFrameProps {
  children: ReactNode;
}

/**
 * Outer Dao Browser window shell. Unlike a typical Chromium browser, Dao has
 * **no separate titlebar** — the macOS traffic-light dots live at the top of
 * the sidebar, and the page address bar lives at the top of the content area.
 * This component is just the outer rounded frame; the chrome is rendered
 * by `BrowserFrameMockup` itself.
 */
export function BrowserFrame({ children }: BrowserFrameProps) {
  return (
    <div className={styles.frame} aria-hidden="true">
      <div className={styles.body}>{children}</div>
    </div>
  );
}
