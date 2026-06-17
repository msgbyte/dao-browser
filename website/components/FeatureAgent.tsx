import { FeatureSection } from './FeatureSection';

export function FeatureAgent() {
  return (
    <FeatureSection
      eyebrow="03 / AI AGENT"
      heading="An assistant that lives next to your tabs."
      body="A native AI sidebar with long-term memory, explicit-run proactive suggestions, and a visible cursor that shows you exactly what it touched. Suggestion cards can appear as you browse, but Dao only calls AI after you choose Run. No vendor lock-in — bring your own model: Gemini, GPT, Claude, or anything else."
      bullets={[
        { icon: 'brain', label: 'Long-term memory' },
        { icon: 'zap', label: 'Explicit-run suggestions' },
        { icon: 'mouse-pointer-2', label: 'Visible actions' },
      ]}
      mockupVariant="agent"
      mockupSide="right"
      learnMoreHref="/agent"
      learnMoreLabel="See how Dao Agent works"
    />
  );
}
