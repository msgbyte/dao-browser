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
  'chat.attach.element.label': 'element',
  'chat.attach.element.dismiss_title': "Don't attach this element",
  'chat.attach.element.pick_tooltip': 'Pick an element from the page',
  'chat.attach.element.pick_active_tooltip': 'Cancel element picking',
  'chat.attach.element.pick_selected': 'Added element: {label}',
  'chat.attach.element.pick_cancelled': 'Element picking cancelled',
  'chat.attach.element.pick_failed': 'Element picking failed',

  // Proactive suggestion card. Detection is local; AI runs only after Run.
  'chat.proactive.cost_hint': 'No AI is used until you click Run.',
  'chat.proactive.run': 'Run',
  'chat.proactive.running': 'Starting…',
  'chat.proactive.dismiss': 'Dismiss',
  'chat.proactive.not_now': 'Not now',
  'chat.proactive.not_now_aria': 'Ignore suggestion for now: {title}',
  'chat.proactive.never_here': "Don't suggest on this page",
  'chat.proactive.never_here_aria':
      "Don't suggest on this page again: {title}",
  'chat.proactive.run_aria': 'Run suggestion: {title}',
  'chat.proactive.dismiss_aria': 'Dismiss suggestion: {title}',
  'chat.proactive.failed': 'Could not start this suggestion',
  'chat.proactive.user_prompt': 'Run suggestion: {title}',
  'chat.proactive.default_user_prompt': 'Help me with this page.',
  'chat.proactive.reason_label': 'Why Dao suggested this',
  'chat.proactive.expected_outcome_label': 'Expected outcome',
  'chat.proactive.context_label': 'Context disclosure',
  'chat.proactive.visible_prompt_reason_header': 'Why Dao suggested this:',
  'chat.proactive.visible_prompt_expected_header': 'Expected outcome:',
  'chat.proactive.default_context_disclosure':
      'Dao will only attach the current page after you click Run.',
  'chat.proactive.default_expected_outcome':
      'Help you make progress on this page.',
  'chat.proactive.context.captures_after_run':
      'Dao will only attach visible page text after you click Run.',
  'chat.proactive.context.no_capture_before_run':
      'No page text is captured before Run.',
  'chat.proactive.reason.sensitive_page':
      'This page looks sensitive, so proactive help stays off until you ask.',
  'chat.proactive.reason.fatigue':
      'Recent dismissals on this domain suggest staying quiet for now.',
  'chat.proactive.reason.typing':
      'You are actively typing, so Dao will wait until the page is less interruptible.',
  'chat.proactive.reason.missing_action_evidence':
      'The page matches this action, but Dao cannot see enough evidence for a useful result yet.',
  'chat.proactive.reason.poor_personal_fit':
      'You have skipped this personalized suggestion often, so Dao will stay quiet here.',
  'chat.proactive.reason.insufficient_content':
      'The page matches this scenario, but there is not enough visible content yet.',
  'chat.proactive.reason.structured_content':
      'The page matches this scenario and shows structured content that should produce a useful result.',
  'chat.proactive.reason.matched_structure':
      'The page matches this scenario and has enough visible structure for a useful result.',
  'chat.proactive.expected.review_code':
      'Produces a concise review with issues, suggestions, and an overall assessment.',
  'chat.proactive.expected.analyze_issue':
      'Produces a short issue summary with key discussion points and next steps.',
  'chat.proactive.expected.analyze_progress':
      'Produces a status readout with blockers, risks, and priority recommendations.',
  'chat.proactive.expected.summarize_doc':
      'Produces a quick summary of the page with key concepts and caveats.',
  'chat.proactive.expected.extract_answer':
      'Produces the likely answer with important caveats pulled into one place.',
  'chat.proactive.expected.default':
      'Produces a focused result for this page.',
  'chat.proactive.attachment_title': 'Proactive suggestion',
  'chat.proactive.title.review_code': 'Review this PR',
  'chat.proactive.title.analyze_issue': 'Analyze this issue',
  'chat.proactive.title.analyze_progress': 'Analyze project progress',
  'chat.proactive.title.summarize_doc': 'Summarize this doc',
  'chat.proactive.title.extract_answer': 'Extract the answer',
  'chat.proactive.repeat_action_title':
      'You usually interact with this page. Want me to help again?',
  'chat.proactive.continue_conversation_title':
      'Last time here, you discussed: {intent}',
  'chat.proactive.continue_conversation_title_fallback':
      'Continue where you left off?',
  'chat.proactive.continue_conversation_prompt':
      'Continue helping me with: {intent}',

  // Per-message action buttons (Copy / Share-as-image / Regenerate).
  'chat.message_actions.copy_tooltip': 'Copy answer text',
  'chat.message_actions.share_tooltip': 'Copy as image',
  'chat.message_actions.regenerate_tooltip': 'Regenerate response',
  'chat.message_actions.rewind_tooltip': 'Rewind to this response',
  'chat.message_actions.more_tooltip': 'More actions',
  'chat.message_actions.edit': 'Edit',
  'chat.message_actions.edit_tooltip': 'Edit message',
  'chat.message_actions.view_context': 'View context',
  'chat.message_actions.context_title': 'Message context',
  'chat.message_actions.context_close': 'Close context',
  'chat.message_actions.save_edit': 'Save',
  'chat.message_actions.cancel_edit': 'Cancel',
  'chat.message_actions.regenerating': 'Regenerating',
  // Transient labels flashed on the action buttons.
  'chat.message_actions.empty': 'Empty',
  'chat.message_actions.empty_edit': 'Message cannot be empty',
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

  // -------- memory SQL browser (dao_memory_app.ts) --------
  'memory.title': 'Memory',
  'memory.subtitle': 'SQLite debug browser',
  'memory.tables': 'Tables',
  'memory.tables_empty': 'No tables found',
  'memory.tables_loading': 'Loading tables…',
  'memory.query_label': 'SQL',
  'memory.max_rows': 'Rows',
  'memory.run': 'Run',
  'memory.running': 'Running…',
  'memory.reset': 'Reset',
  'memory.result_empty': 'Run a read-only SQL query to inspect memory data.',
  'memory.result_count': '{count} rows',
  'memory.result_count_truncated': '{count} rows · truncated',
  'memory.result_count_filtered': '{visible} / {count} rows',
  'memory.result_count_filtered_truncated':
      '{visible} / {count} rows · truncated',
  'memory.error_prefix': 'Error',
  'memory.filter_placeholder': 'Filter',
  'memory.filter_button': 'Filter',
  'memory.filter_button_count': 'Filter ({count})',
  'memory.filter_panel_title': 'Column filters',
  'memory.clear_filters': 'Clear',
  'memory.null': 'NULL',
  'memory.copy_sql': 'Copy SQL',
  'memory.copied': 'Copied',

  // -------- custom WebUI index (dao_index_app.ts) --------
  'index.loading': 'Loading pages…',
  'index.title': 'Dao Pages',
  'index.subtitle': 'Custom Dao browser pages for tools and debugging.',
  'index.page.index.title': 'Index',
  'index.page.index.desc': 'Directory of custom Dao WebUI pages.',
  'index.page.agent.title': 'Agent',
  'index.page.agent.desc': 'AI assistant sidebar and debugging surface.',
  'index.page.skills.title': 'Skills',
  'index.page.skills.desc': 'Skill manager for Agent commands and tools.',
  'index.page.dream.title': 'Dream',
  'index.page.dream.desc': 'Nightly behavior-learning reports.',
  'index.page.memory.title': 'Memory',
  'index.page.memory.desc': 'Read-only SQLite browser for local Agent memory.',
  'index.page.sidebar.title': 'Sidebar',
  'index.page.sidebar.desc': 'Dao vertical sidebar WebUI shell.',
  'index.page.welcome.title': 'Welcome',
  'index.page.welcome.desc': 'First-run onboarding page.',

  // -------- settings sub-tab labels (dao_settings_view.ts) --------
  'settings.tabs.general_label': 'General',
  'settings.tabs.soul_label': 'Soul',
  'settings.tabs.tools_label': 'Tools',
  'settings.tabs.memory_label': 'Memory',
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
  'settings.general.debug_mode_name': 'Debug Mode',
  'settings.general.debug_mode_desc':
      'Show a context inspector on user messages for local debugging',
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

  // -------- settings.dream (dao_settings_view.ts → renderDream_) --------
  'settings.dream.title': 'Dream Analysis',
  'settings.dream.desc':
      'At night, while you are away, Dao reviews your day and learns ' +
      'your habits.',
  'settings.dream.enable_name': 'Enable Dream Analysis',
  'settings.dream.enable_desc':
      'Each night, the domains you visited, page titles and search ' +
      'keywords from your day are sent to your configured AI provider ' +
      'for analysis. Excluded domains are omitted before analysis.',
  'settings.dream.excluded_domains_label': 'Excluded domains',
  'settings.dream.excluded_domains_placeholder': 'example.com',
  'settings.dream.excluded_domains_add': 'Add',
  'settings.dream.excluded_domains_remove': 'Remove {domain}',
  'settings.dream.excluded_add_failed': 'Could not add domain: {error}',
  'settings.dream.excluded_remove_failed':
      'Could not remove domain: {error}',
  'settings.dream.debug_name': 'Debug mode',
  'settings.dream.debug_desc':
      'Store the exact input sent to the AI for each dream run and show ' +
      'it on the report card.',
  'settings.dream.run_now_button': 'Dream now',
  'settings.dream.run_running': 'Dreaming…',
  'settings.dream.history_button': 'View dream history',
  'settings.dream.run_done_toast': 'Dream report generated',
  'settings.dream.run_failed_toast': 'Dream run failed: {error}',
  'settings.dream.no_key_toast':
      'Configure an AI provider and API key first',

  // -------- chat.dream (dao_chat_view.ts → renderDreamCard_) --------
  'chat.dream.card_title': 'Last night\'s dream report',
  'chat.dream.card_date': 'About your day on {date}',
  'chat.dream.expand': 'Read the report',
  'chat.dream.collapse': 'Collapse',
  'chat.dream.dismiss': 'Dismiss dream report',
  'chat.dream.habits_title': 'I think I noticed…',
  'chat.dream.habit_confirm': 'Yes',
  'chat.dream.habit_reject': 'Not really',
  'chat.dream.habit_confirmed': 'Got it, remembered',
  'chat.dream.habit_rejected': 'Okay, forgotten',
  'chat.dream.contradict_prefix': 'I noticed a change:',
  'chat.dream.debug_title': 'Debug: inputs used for this dream',
  'dream.page.eyebrow': 'Dream Analysis',
  'dream.page.title': 'Dream Report',
  'dream.page.loading': 'Loading dream report...',
  'dream.page.empty_title': 'No dream report yet',
  'dream.page.empty_desc':
      'Run Dream Analysis from Agent settings to generate a report.',
  'dream.page.error': 'Failed to load dream report: {error}',
  'dream.page.history_title': 'History',
  'dream.page.copy_image': 'Copy image',
  'dream.page.copy_image_copied': 'Copied image',
  'dream.page.copy_image_failed': 'Copy failed',
  'dream.page.rerun_report': 'Rerun report',
  'dream.page.rerun_running': 'Dreaming…',
  'dream.page.rerun_failed': 'Rerun failed: {error}',
  'dream.page.source_domains_title': 'Domains used in this report',
  'dream.page.source_domains_add': 'Add to blacklist',
  'dream.page.source_domains_excluded': 'Already blacklisted',
  'dream.page.excluded_domains_adding': 'Adding...',
  'dream.page.excluded_add_failed': 'Add failed: {error}',
  'dream.page.source_domains_empty':
      'No domains were captured for this report. Rerun it to refresh choices.',
  'dream.debug.generated_at': 'Generated at: {time}',
  'dream.share.footer': 'Dreamed by Dao Browser',
  'dream.trigger.nightly': 'Nightly',
  'dream.trigger.catchup': 'Catch-up',
  'dream.trigger.manual': 'Manual',

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
