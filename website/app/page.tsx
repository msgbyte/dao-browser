import { TopNav } from '@/components/TopNav';
import { Hero } from '@/components/Hero';
import { FeatureSidebar } from '@/components/FeatureSidebar';
import { FeatureCommandBar } from '@/components/FeatureCommandBar';
import { FeatureAgent } from '@/components/FeatureAgent';
import { FeatureHostSkills } from '@/components/FeatureHostSkills';
import { FeatureContentFirst } from '@/components/FeatureContentFirst';
import { FeatureKeyboardFirst } from '@/components/FeatureKeyboardFirst';
import { FeatureGrid } from '@/components/FeatureGrid';
import { DownloadCTA } from '@/components/DownloadCTA';
import { Footer } from '@/components/Footer';
import {
  DEFAULT_DESCRIPTION,
  DEFAULT_TITLE,
  buildWebPageJsonLd,
  jsonLdScriptProps,
} from '@/lib/seo';

const HOME_PAGE_JSON_LD = buildWebPageJsonLd({
  title: DEFAULT_TITLE,
  description: DEFAULT_DESCRIPTION,
  path: '/',
});

export default function HomePage() {
  return (
    <>
      <TopNav />
      <main>
        <Hero />
        <FeatureSidebar />
        <FeatureCommandBar />
        <FeatureAgent />
        <FeatureHostSkills />
        <FeatureContentFirst />
        <FeatureKeyboardFirst />
        <FeatureGrid />
        <DownloadCTA />
      </main>
      <Footer />
      <script {...jsonLdScriptProps(HOME_PAGE_JSON_LD)} />
    </>
  );
}
