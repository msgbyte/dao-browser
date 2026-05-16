import { FeatureSection } from './FeatureSection';
import { KeyboardVisual } from './ui/KeyboardVisual';

export function FeatureKeyboardFirst() {
  return (
    <FeatureSection
      eyebrow="06 / KEYBOARD FIRST"
      heading="A few keystrokes away from anywhere."
      body="Learn a handful of Dao Browser shortcuts and you'll fall in love with using it. Tabs, sidebar, address bar, AI — all one keypress away. Your hands never have to leave the keyboard."
      bullets={[
        { icon: 'command', label: 'Cmd + L for the command bar' },
        { icon: 'panel-left', label: 'Cmd + S to toggle the sidebar' },
        { icon: 'sparkles', label: 'Cmd + E for the AI Agent' },
      ]}
      customVisual={<KeyboardVisual />}
      mockupSide="left"
    />
  );
}
