// Simplified Chinese translations for Dao agent WebUI.
// Hand-authored, used as the canonical reference for tone / register when
// scripts/i18n-translate.sh produces other locale dictionaries.
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,

  'chat.gauge.tooltip_no_capacity': '预估上下文:{tokens} tokens',
  'chat.gauge.tooltip_with_capacity':
      '预估上下文:{tokens} tokens / {capacity}({percent}%)',

  'chat.compact.cancel_tooltip': '取消摘要',
  'chat.compact.start_tooltip': '将历史压缩为一条摘要消息',
  'chat.compact.compacting': '压缩中…',
  'chat.compact.label': '压缩',
  'chat.compact.cancelled': '已取消压缩',
  'chat.compact.success': '已压缩 {count} 条消息 → 1 条摘要',
  'chat.compact.failed': '压缩失败:{error}',

  'chat.empty_guide.title': '需要帮你做什么?',
  'chat.empty_guide.hint': '询问当前页面、总结选中文本,或运行一个技能。',

  'chat.attach.page.dismiss_title': '不附带当前页面',
  'chat.attach.selection.label': '选中文本',
  'chat.attach.selection.dismiss_title': '不附带这段选中文本',

  'chat.message_actions.copy_tooltip': '复制回答文本',
  'chat.message_actions.share_tooltip': '复制为图片',
  'chat.message_actions.regenerate_tooltip': '重新生成回答',
  'chat.message_actions.empty': '空',
  'chat.message_actions.copied': '已复制',
  'chat.message_actions.shared': '已复制图片',
  'chat.message_actions.failed': '失败',

  'chat.toast.wait_for_turn': '请等待当前回合结束',

  'chat.skill_picker.aria_label': '技能选择器',
  'chat.skill_picker.title': '技能',
  'chat.skill_picker.hint': '↑↓ 切换 · Enter 选中 · Esc 关闭',
};

export default dict;
