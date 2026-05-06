import { BrowserFrame } from './BrowserFrame';
import { BrowserFrameMockup } from './BrowserFrameMockup';
import styles from './HeroAnimation.module.css';

/**
 * Continuous looping product demo for the hero. Three Dao Browser scenes
 * crossfade in turn — sidebar / command bar / agent — driven entirely by
 * CSS `@keyframes`. No JavaScript, no video file, themes adapt automatically.
 *
 * Inspired by the looping product demos on oc-claw.ai and Linear's marketing
 * page. Pure CSS keeps it crisp on every screen and respects
 * `prefers-reduced-motion`.
 */
export function HeroAnimation() {
  return (
    <div className={styles.stack} aria-label="Dao Browser product demo">
      <div className={`${styles.scene} ${styles.scene1}`}>
        <BrowserFrame>
          <BrowserFrameMockup variant="sidebar" />
        </BrowserFrame>
      </div>
      <div className={`${styles.scene} ${styles.scene2}`}>
        <BrowserFrame>
          <BrowserFrameMockup variant="commandbar" />
        </BrowserFrame>
      </div>
      <div className={`${styles.scene} ${styles.scene3}`}>
        <BrowserFrame>
          <BrowserFrameMockup variant="agent" />
        </BrowserFrame>
      </div>
    </div>
  );
}
