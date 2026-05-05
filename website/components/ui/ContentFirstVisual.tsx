import styles from './ContentFirstVisual.module.css';

/**
 * Side-by-side comparison: a "traditional" browser with thick chrome stealing
 * space from the page, vs. Dao with the page taking center stage.
 */
export function ContentFirstVisual() {
  return (
    <div className={styles.root} aria-hidden="true">
      <div className={styles.compare}>
        {/* Traditional */}
        <figure className={styles.window}>
          <figcaption className={styles.caption}>Traditional</figcaption>
          <div className={styles.frame}>
            <div className={styles.titlebarThick}>
              <span className={styles.dot} />
              <span className={styles.dot} />
              <span className={styles.dot} />
            </div>
            <div className={styles.tabsThick}>
              <span className={styles.tabPill} />
              <span className={styles.tabPill} />
              <span className={styles.tabPillActive} />
            </div>
            <div className={styles.toolbarThick}>
              <span className={styles.urlPill} />
            </div>
            <div className={styles.bookmarkBar}>
              <span className={styles.bookmark} />
              <span className={styles.bookmark} />
              <span className={styles.bookmark} />
              <span className={styles.bookmark} />
            </div>
            <div className={styles.content}>
              <span className={styles.contentLabel}>Content</span>
            </div>
          </div>
        </figure>

        {/* Dao */}
        <figure className={styles.window}>
          <figcaption className={styles.captionAccent}>Dao</figcaption>
          <div className={styles.frame}>
            <div className={styles.titlebarThin}>
              <span className={styles.dot} />
              <span className={styles.dot} />
              <span className={styles.dot} />
            </div>
            <div className={styles.contentBig}>
              <span className={styles.contentLabel}>Content</span>
            </div>
          </div>
        </figure>
      </div>
    </div>
  );
}
