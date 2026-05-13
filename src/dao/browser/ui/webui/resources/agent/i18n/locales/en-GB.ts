// Auto-translated by scripts/i18n-translate.py.
// Lang: en-GB. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Estimated context: {tokens} tokens',
  'chat.gauge.tooltip_with_capacity': 'Estimated context: {tokens} tokens / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Cancel summarisation',
  'chat.compact.start_tooltip': 'Summarise history into a single context message',
  'chat.compact.compacting': 'Compacting…',
  'chat.compact.label': 'Compact',
  'chat.compact.cancelled': 'Compaction cancelled',
  'chat.compact.success': 'Compacted {count} messages → 1 summary',
  'chat.compact.failed': 'Compact failed: {error}',
  'chat.empty_guide.title': 'How can I help?',
  'chat.empty_guide.hint': 'Ask about the current page, summarise selections, or run a skill.',
  'chat.attach.page.dismiss_title': 'Don\'t attach this page',
  'chat.attach.selection.label': 'selection',
  'chat.attach.selection.dismiss_title': 'Don\'t attach this selection',
  'chat.message_actions.copy_tooltip': 'Copy answer text',
  'chat.message_actions.share_tooltip': 'Copy as image',
  'chat.message_actions.regenerate_tooltip': 'Regenerate response',
  'chat.message_actions.empty': 'Empty',
  'chat.message_actions.copied': 'Copied',
  'chat.message_actions.shared': 'Shared',
  'chat.message_actions.failed': 'Failed',
  'chat.toast.wait_for_turn': 'Wait for the current turn to finish',
  'chat.skill_picker.aria_label': 'Skill picker',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint': '↑↓ to navigate · Enter to select · Esc to dismiss',
};

export default dict;
