import { TopNav } from '@/components/TopNav';
import { Hero } from '@/components/Hero';
import { FeatureSidebar } from '@/components/FeatureSidebar';
import { FeatureCommandBar } from '@/components/FeatureCommandBar';
import { FeatureAgent } from '@/components/FeatureAgent';
import { FeatureGrid } from '@/components/FeatureGrid';
import { DownloadCTA } from '@/components/DownloadCTA';
import { Footer } from '@/components/Footer';

export default function HomePage() {
  return (
    <>
      <TopNav />
      <main>
        <Hero />
        <FeatureSidebar />
        <FeatureCommandBar />
        <FeatureAgent />
        <FeatureGrid />
        <DownloadCTA />
      </main>
      <Footer />
    </>
  );
}
