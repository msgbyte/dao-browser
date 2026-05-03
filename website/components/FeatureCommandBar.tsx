import { FeatureSection } from './FeatureSection';

export function FeatureCommandBar() {
  return (
    <FeatureSection
      eyebrow="02 / COMMAND BAR"
      heading="Cmd+T, Cmd+L, ask anything."
      body="Spotlight-style command bar with URL detection, search routing, and ghost-text completion. Press Esc on a fresh tab to cancel — Dao remembers where you came from."
      bullets={[
        { icon: 'command', label: 'New tab' },
        { icon: 'search', label: 'Smart routing' },
        { icon: 'sparkles', label: 'Ask AI' },
      ]}
      mockupVariant="commandbar"
      mockupSide="left"
    />
  );
}
