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

  // -------- settings.workspace (dao_settings_view.ts → renderWorkspace_) -----
  'settings.workspace.subtab_label': 'Workspace',
  'settings.workspace.section_title': 'Agent Workspace',
  'settings.workspace.section_desc':
      'The agent can read and write text files in a sandboxed folder on '
      + 'disk so notes and drafts persist across conversations. Files '
      + 'are stored in your Dao profile and never uploaded.',
  'settings.workspace.root_label': 'Folder',
  'settings.workspace.root_reveal_button': 'Show in Finder',
  'settings.workspace.usage_label': 'Storage used',
  // {used} and {cap} are pre-formatted strings like "4.2 MB" / "100 MB".
  // {percent} is an integer 0–100.
  'settings.workspace.usage_value': '{used} of {cap} ({percent}%)',
  'settings.workspace.file_count_label': 'Files',
  // {count} is an integer; {cap} is the file-count cap, e.g. 500.
  'settings.workspace.file_count_value': '{count} of {cap}',
  'settings.workspace.activity_title': 'Recent activity',
  'settings.workspace.activity_empty':
      'No workspace activity yet. The agent will write here when it uses '
      + 'workspace_write, workspace_edit, or apply_patch.',
  'settings.workspace.activity_loading': 'Loading…',
  // Activity row template — {when} is a short relative time, {op} is one
  // of "write" / "edit" / "apply_patch", {path} is the workspace-relative
  // path.
  'settings.workspace.activity_row': '{when} · {op} · {path}',
  'settings.workspace.activity_error':
      'Could not load recent activity: {error}',

  // -------- settings sub-tab labels (dao_settings_view.ts) --------
  'settings.tabs.general_label': 'General',
  'settings.tabs.soul_label': 'Soul',
  'settings.tabs.tools_label': 'Tools',
  'settings.tabs.skills_label': 'Skills',
  'settings.tabs.stats_label': 'Stats',

  // Shared confirm-dialog button used by both memory and stats reset.
  'settings.dialog.cancel': 'Cancel',

  // -------- settings.general (dao_settings_view.ts → renderGeneral_) --------
  'settings.general.api_connection_title': 'API Connection',
  'settings.general.api_connection_desc':
      'Configure the LLM provider for the agent. Each provider keeps '
      + 'its own credentials.',
  'settings.general.provider_label': 'Provider',
  'settings.general.api_key_label': 'API Key',
  'settings.general.base_url_label': 'Base URL',
  'settings.general.model_label': 'Model',
  'settings.general.display_title': 'Display',
  'settings.general.show_tool_details_name': 'Show Tool Call Details',
  'settings.general.show_tool_details_desc':
      'Expand every tool call input / output by default',
  'settings.general.session_title': 'Session',
  'settings.general.resume_session_name': 'Resume Last Session',
  'settings.general.resume_session_desc':
      'Reopen the most recent conversation when the agent panel opens',
  'settings.general.stale_threshold_name': 'Stale Session Threshold',
  'settings.general.stale_threshold_desc':
      'Start a new conversation if the last message is older than this '
      + 'many hours (0 = always resume)',
  'settings.general.stale_threshold_aria': 'Stale session threshold in hours',
  'settings.general.hours_unit': 'hours',

  // -------- settings.soul (dao_settings_view.ts → renderSoul_) --------
  'settings.soul.title': 'Soul Prompt',
  'settings.soul.desc':
      "Define the AI agent's personality and behavior. This is used as "
      + 'the system prompt in every conversation.',
  'settings.soul.placeholder': 'Enter your soul prompt here...',
  'settings.soul.save_button': 'Save',
  'settings.soul.reset_button': 'Reset to Default',
  // Transient status flashed next to the Save button.
  'settings.soul.saved_status': 'Saved',
  'settings.soul.reset_status': 'Reset to default',

  // -------- settings.memory (dao_settings_view.ts → renderMemory_) --------
  'settings.memory.title': 'Memory',
  'settings.memory.desc':
      'Control how the Agent remembers and learns from your interactions.',
  'settings.memory.enable_name': 'Enable Memory',
  'settings.memory.enable_desc': 'Master switch for all memory features',
  'settings.memory.proactive_name': 'Proactive Suggestions',
  'settings.memory.proactive_desc':
      'Show tips based on your browsing context',
  'settings.memory.threshold_name': 'Suggestion Threshold',
  'settings.memory.threshold_aria': 'Suggestion threshold',
  // Radio labels for the threshold segment selector.
  'settings.memory.threshold_quiet': 'Quiet',
  'settings.memory.threshold_balanced': 'Balanced',
  'settings.memory.threshold_active': 'Active',
  'settings.memory.page_context_name': 'Page Context Memory',
  'settings.memory.page_context_desc':
      'Remember interactions on specific pages',
  'settings.memory.conversation_name': 'Conversation History',
  'settings.memory.conversation_desc': 'Save chat history across sessions',
  'settings.memory.usage_title': 'Memory Usage',
  'settings.memory.conversations': 'Conversations',
  'settings.memory.preferences': 'Preferences',
  'settings.memory.episodes': 'Episodes',
  // {kb} is a pre-formatted string like "4.2".
  'settings.memory.total_format': 'Total: {kb} KB',
  'settings.memory.clear_button': 'Clear All Memory',
  'settings.memory.clear_dialog_title': 'Clear All Memory?',
  'settings.memory.clear_dialog_desc':
      'This will permanently erase all conversations, preferences, and '
      + 'page episodes. This action cannot be undone.',
  'settings.memory.clear_confirm': 'Clear All',
  'settings.memory.toast_cleared': 'All memory cleared',
  'settings.memory.toast_clear_failed': 'Failed to clear memory',

  // -------- settings.stats (dao_settings_view.ts → renderStats_) --------
  'settings.stats.title': 'Agent Statistics',
  // {date} is a localized short date like "May 18, 2026".
  'settings.stats.since_format': 'Usage since {date}',
  'settings.stats.api_calls': 'API Calls',
  'settings.stats.tool_calls': 'Tool Calls',
  // {inTok} / {outTok} are pre-formatted compact numbers like "12.3K".
  'settings.stats.total_tokens_format':
      'Total Tokens ({inTok} in / {outTok} out)',
  'settings.stats.estimated_cost': 'Estimated Cost',
  'settings.stats.tool_breakdown': 'Tool Breakdown',
  'settings.stats.table_tool_header': 'Tool',
  'settings.stats.table_calls_header': 'Calls',
  'settings.stats.empty': 'No tool calls recorded yet.',
  'settings.stats.reset_button': 'Reset Statistics',
  'settings.stats.reset_dialog_title': 'Reset Statistics?',
  'settings.stats.reset_dialog_desc':
      'This will clear all recorded API calls, tool usage counts, token '
      + 'usage, and cost data. This action cannot be undone.',
  'settings.stats.reset_confirm': 'Reset',
  'settings.stats.toast_reset': 'Statistics reset',

  // -------- settings.tools (dao_settings_view.ts → renderTools_*) --------
  'settings.tools.title': 'Tools',
  'settings.tools.desc':
      'Choose which tools the agent can call. Disabled tools are hidden '
      + 'from the model, so it will not attempt to use them. Changes take '
      + 'effect on the next turn of any open chat.',
  // {group} is the translated tool group label.
  'settings.tools.toggle_all_aria': 'Toggle all {group} tools',
  // {name} is the technical tool identifier (kept untranslated).
  'settings.tools.toggle_one_aria': 'Toggle {name}',
  // Web search source picker, shown only inside the "web" tool group.
  'settings.tools.search_source_label': 'Search source',
  'settings.tools.search_auto': 'Auto',
  'settings.tools.search_provider_only': 'Provider only',
  'settings.tools.search_duckduckgo_only': 'DuckDuckGo only',
  // Tool group display names (TOOL_GROUPS in tool_catalog.ts owns the ids;
  // the labels are translated here so we don't have to fan i18n into the
  // catalog itself).
  'settings.tools.group.page': 'Page',
  'settings.tools.group.tabs': 'Tabs',
  'settings.tools.group.devtools': 'DevTools',
  'settings.tools.group.memory-skills': 'Memory & Skills',
  'settings.tools.group.web': 'Web',
  'settings.tools.group.workspace': 'Workspace',

  // -------- settings.skills (dao_settings_view.ts → renderSkills_) --------
  'settings.skills.title': 'Skills',
  'settings.skills.desc':
      'Manage slash-command skills for the agent. Open the Skill Manager '
      + 'to create, edit, and delete skills in a full-page editor.',
  'settings.skills.open_manager_button': 'Open Skill Manager',
  'settings.skills.open_directory_button': 'Open Skills Directory',

  // -------- skills (dao_skill_manager_view.ts) --------
  // {name} is the technical skill identifier (kept untranslated).
  'skills.toggle.enable_aria': 'Enable skill {name}',
  'skills.toggle.disable_aria': 'Disable skill {name}',
  'skills.toggle.failed': 'Failed to update skill',
};

export default dict;
