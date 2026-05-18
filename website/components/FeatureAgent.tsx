import { FeatureSection } from './FeatureSection';

export function FeatureAgent() {
  return (
    <FeatureSection
      eyebrow="03 / AI AGENT"
      heading="An assistant that lives next to your tabs."
      body="A native AI sidebar with long-term memory, proactive suggestions on every navigation, and a visible cursor that shows you exactly what it touched. No vendor lock-in — bring your own model: Gemini, GPT, Claude, or anything else."
      bullets={[
        { icon: 'brain', label: 'Long-term memory' },
        { icon: 'zap', label: 'Proactive' },
        { icon: 'mouse-pointer-2', label: 'Visible actions' },
      ]}
      mockupVariant="agent"
      mockupSide="right"
      learnMoreHref="/agent"
      learnMoreLabel="See how Dao Agent works"
    />
  );
}
