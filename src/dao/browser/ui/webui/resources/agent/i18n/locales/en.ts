// English source strings for Dao agent WebUI. This file is the canonical
// dictionary — all other locales import it for fallback, and the
// scripts/i18n-translate.sh translator uses it as input. Keep keys
// stable: they are referenced from t('key') call sites in TS files.
//
// Naming convention: <view>.<area>.<purpose>. Use {placeholder} tokens
// for runtime values, e.g. t('chat.compact.success', { count: 4 }).
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  // -------- chat (dao_chat_view.ts) --------
  // Context size gauge tooltip; {tokens} is current estimated tokens.
  // Variant with capacity is shown when the active model has a known limit.
  'chat.gauge.tooltip_no_capacity': 'Estimated context: {tokens} tokens',
  'chat.gauge.tooltip_with_capacity':
      'Estimated context: {tokens} tokens / {capacity} ({percent}%)',

  // Compact bar (manual conversation summarization affordance).
  'chat.compact.cancel_tooltip': 'Cancel summarization',
  'chat.compact.start_tooltip':
      'Summarize history into a single context message',
  'chat.compact.compacting': 'Compacting…',
  'chat.compact.label': 'Compact',
  'chat.compact.cancelled': 'Compaction cancelled',
  'chat.compact.success': 'Compacted {count} messages → 1 summary',
  'chat.compact.failed': 'Compact failed: {error}',

  // Empty state shown before the first turn.
  'chat.empty_guide.title': 'How can I help?',
  'chat.empty_guide.hint':
      'Ask about the current page, summarize selections, or run a skill.',

  // Attachment chips (page / selection chips above the composer).
  'chat.attach.page.dismiss_title': "Don't attach this page",
  'chat.attach.selection.label': 'selection',
  'chat.attach.selection.dismiss_title': "Don't attach this selection",

  // Per-message action buttons (Copy / Share-as-image / Regenerate).
  'chat.message_actions.copy_tooltip': 'Copy answer text',
  'chat.message_actions.share_tooltip': 'Copy as image',
  'chat.message_actions.regenerate_tooltip': 'Regenerate response',
  // Transient labels flashed on the action buttons.
  'chat.message_actions.empty': 'Empty',
  'chat.message_actions.copied': 'Copied',
  'chat.message_actions.shared': 'Shared',
  'chat.message_actions.failed': 'Failed',

  // Per-code-block "Insert into focused input" button (next to vendor
  // copy-button inside <code-block>). Visible only when the active tab
  // has a focused text input.
  'chat.code_block.insert_tooltip': 'Insert into focused input on this page',
  'chat.code_block.inserted': 'Inserted',
  'chat.code_block.no_input': 'No input focused',
  'chat.code_block.empty': 'Empty',

  // Toast shown when the user submits while a turn is still streaming.
  'chat.toast.wait_for_turn': 'Wait for the current turn to finish',

  // Skill picker dropdown.
  'chat.skill_picker.aria_label': 'Skill picker',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint':
      '↑↓ to navigate · Enter to select · Esc to dismiss',
};

export default dict;
