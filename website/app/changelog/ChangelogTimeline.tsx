'use client';

import { useEffect, useState } from 'react';
import type { ChangelogPlatform } from '@/lib/changelog';
import copy from '@/locales/en/changelog.json';
import { InlineMarkdown } from './InlineMarkdown';
import styles from './changelog.module.css';

export function ChangelogTimeline({ platforms }: {
  platforms: ChangelogPlatform[];
}) {
  const [anchor, setAnchor] = useState('');
  const active = platforms.find(({ id, releases }) =>
    anchor === id || releases.some(({ version }) => anchor === `${id}-${version}`),
  )?.id ?? 'desktop';

  useEffect(() => {
    const followHash = () => setAnchor(window.location.hash.slice(1));
    followHash();
    window.addEventListener('hashchange', followHash);
    return () => window.removeEventListener('hashchange', followHash);
  }, []);

  useEffect(() => {
    if (anchor) document.getElementById(anchor)?.scrollIntoView({ block: 'start' });
  }, [anchor]);

  return (
    <>
      <div className={styles.controls}>
        <nav className={styles.platforms} aria-label={copy.platforms}>
          {platforms.map(({ id, title }) => (
            <a key={id} href={`#${id}`} aria-current={active === id ? 'true' : undefined}>
              {title}
            </a>
          ))}
        </nav>
      </div>

      {platforms.map(({ id, title, releases }) => (
        <section key={id} id={id} hidden={active !== id} aria-label={title} className={styles.platform}>
          {releases.length === 0 ? <p className={styles.empty}>{copy.empty}</p> : (
            <ol className={styles.timeline}>
              {releases.map((release, index) => {
                const releaseId = `${id}-${release.version}`;
                return (
                  <li key={release.version} id={releaseId} className={styles.release}>
                    <div className={styles.meta}>
                      <div className={styles.versionLine}>
                        <h2><a href={`#${releaseId}`}>{release.version}</a></h2>
                        {index === 0 && <span className={styles.latest}>{copy.latest}</span>}
                      </div>
                      <time dateTime={release.date}>
                        {new Intl.DateTimeFormat('en', { year: 'numeric', month: 'short', day: 'numeric', timeZone: 'UTC' }).format(new Date(`${release.date}T00:00:00Z`))}
                      </time>
                    </div>
                    <ul className={styles.entries}>
                      {release.entries.map((entry, entryIndex) => <li key={entryIndex}><InlineMarkdown text={entry} /></li>)}
                    </ul>
                  </li>
                );
              })}
            </ol>
          )}
        </section>
      ))}
    </>
  );
}
