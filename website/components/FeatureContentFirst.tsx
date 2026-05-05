import { FeatureSection } from './FeatureSection';
import { ContentFirstVisual } from './ui/ContentFirstVisual';

export function FeatureContentFirst() {
  return (
    <FeatureSection
      eyebrow="04 / CONTENT FIRST"
      heading="The web, with more room to breathe."
      body="We work hard to give you more space for the content that matters. Read, watch, and create with the maximum of your screen — not the maximum of the browser around it."
      bullets={[
        { icon: 'panel-left', label: 'Collapsible sidebar' },
        { icon: 'columns-2', label: 'Edge-to-edge canvas' },
        { icon: 'palette', label: 'Adapts to the page' },
      ]}
      customVisual={<ContentFirstVisual />}
      mockupSide="left"
    />
  );
}
