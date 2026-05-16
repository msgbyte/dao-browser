import { FeatureSection } from './FeatureSection';
import { HostSkillsVisual } from './ui/HostSkillsVisual';

export function FeatureHostSkills() {
  return (
    <FeatureSection
      eyebrow="04 / HOST-AWARE SKILLS"
      heading="Skills tuned to where you are."
      body="The agent learns site-specific workflows. Drop a skill into a host and Dao will pick it up the next time you land on that domain — different paths for GitHub, YouTube, X, your internal dashboards. The longer you use a site, the more your browser knows about it."
      bullets={[
        { icon: 'globe', label: 'Per-host skill scopes' },
        { icon: 'zap', label: 'Hot-loaded on navigation' },
        { icon: 'code', label: 'Bring your own skills' },
      ]}
      customVisual={<HostSkillsVisual />}
      mockupSide="left"
    />
  );
}
