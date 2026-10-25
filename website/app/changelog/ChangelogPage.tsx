import Link from 'next/link';
import { TopNav } from '@/components/TopNav';
import { Footer } from '@/components/Footer';
import { loadChangelog } from '@/lib/changelog';
import { createMetadata } from '@/lib/seo';
import copy from '@/locales/en/changelog.json';
import { ChangelogTimeline } from './ChangelogTimeline';
import styles from './changelog.module.css';

export const changelogMetadata = createMetadata({
  title: copy.metadataTitle,
  description: copy.metadataDescription,
  path: '/changelog',
});

export async function ChangelogPage() {
  const platforms = await loadChangelog();
  return (
    <>
      <TopNav />
      <main className={styles.root} lang="en">
        <header className={styles.header}>
          <p className={styles.eyebrow}>{copy.nav}</p>
          <h1>{copy.title}</h1>
          <p className={styles.description}>{copy.description}</p>
        </header>
        <ChangelogTimeline platforms={platforms} />
        <p className={styles.history}>{copy.history} <Link href="/history">{copy.historyLink}</Link></p>
      </main>
      <Footer />
    </>
  );
}
