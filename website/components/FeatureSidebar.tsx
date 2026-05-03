import { FeatureSection } from './FeatureSection';

export function FeatureSidebar() {
  return (
    <FeatureSection
      id="features"
      eyebrow="01 / VERTICAL TABS"
      heading="Tabs that read like a list, not a strip."
      body="Drag to resize from 150 to 400 pixels. Collapse to 4 with one keystroke. Group with folders, switch with spaces, pin what you visit daily."
      bullets={[
        { icon: 'panel-left', label: 'Collapse' },
        { icon: 'folder', label: 'Folders' },
        { icon: 'pin', label: 'Favorites' },
      ]}
      mockupVariant="sidebar"
      mockupSide="right"
    />
  );
}
