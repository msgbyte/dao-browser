import Link from 'next/link';
import { TopNav } from '@/components/TopNav';
import { Footer } from '@/components/Footer';
import { loadChangelog, type ChangelogLocale } from '@/lib/changelog';
import { createMetadata } from '@/lib/seo';
import en from '@/locales/en/changelog.json';
import zh from '@/locales/zh-CN/changelog.json';
import { ChangelogTimeline } from './ChangelogTimeline';
import styles from './changelog.module.css';

export function changelogMetadata(locale: ChangelogLocale) {
  const copy = locale === 'zh-CN' ? zh : en;
  const path = locale === 'en' ? '/changelog' : '/changelog/zh-CN';
  const metadata = createMetadata({ title: copy.metadataTitle, description: copy.metadataDescription, path });
  return {
    ...metadata,
    alternates: { ...metadata.alternates, languages: { en: '/changelog', 'zh-CN': '/changelog/zh-CN' } },
  };
}

export async function ChangelogPage({ locale }: { locale: ChangelogLocale }) {
  const copy = locale === 'zh-CN' ? zh : en;
  const platforms = await loadChangelog(locale);
  return (
    <>
      <TopNav changelogLocale={locale} />
      <main className={styles.root} lang={locale}>
        <header className={styles.header}>
          <p className={styles.eyebrow}>{copy.nav}</p>
          <h1>{copy.title}</h1>
          <p className={styles.description}>{copy.description}</p>
        </header>
        <ChangelogTimeline platforms={platforms} locale={locale} />
        <p className={styles.history}>{copy.history} <Link href="/history">{copy.historyLink}</Link></p>
      </main>
      <Footer changelogLocale={locale} />
    </>
  );
}
