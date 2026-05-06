import { BrowserFrame } from './BrowserFrame';
import { BrowserFrameMockup } from './BrowserFrameMockup';
import styles from './HeroAnimation.module.css';

/**
 * Continuous looping product demo for the hero. Three Dao Browser scenes —
 * sidebar / command bar / agent — share a single 18s CSS keyframe cycle and
 * are offset by 6s each via negative `animation-delay`. The fade-out window
 * of one scene aligns 1:1 with the fade-in window of the next, so the
 * handoff is a true crossfade with no empty frame in between.
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
