// Auto-translated by scripts/i18n-translate.py.
// Lang: zh-HK. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': '估計內容：{tokens} 個標記',
  'chat.gauge.tooltip_with_capacity': '估計內容：{tokens} 個標記 / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': '取消摘要',
  'chat.compact.start_tooltip': '將歷史記錄摘要為單一內容訊息',
  'chat.compact.compacting': '壓縮中…',
  'chat.compact.label': '壓縮',
  'chat.compact.cancelled': '壓縮已取消',
  'chat.compact.success': '已壓縮 {count} 則訊息 → 1 則摘要',
  'chat.compact.failed': '壓縮失敗：{error}',
  'chat.empty_guide.title': '有什麼可以幫到你？',
  'chat.empty_guide.hint': '詢問當前頁面、摘要選擇或運行技能。',
  'chat.attach.page.dismiss_title': '不要附加此頁面',
  'chat.attach.selection.label': '選擇',
  'chat.attach.selection.dismiss_title': '不要附加此選擇',
  'chat.message_actions.copy_tooltip': '複製答案文字',
  'chat.message_actions.share_tooltip': '複製為圖片',
  'chat.message_actions.regenerate_tooltip': '重新生成回應',
  'chat.message_actions.empty': '空',
  'chat.message_actions.copied': '已複製',
  'chat.message_actions.shared': '已分享',
  'chat.message_actions.failed': '失敗',
  'chat.toast.wait_for_turn': '等待當前回合完成',
  'chat.skill_picker.aria_label': '技能選擇器',
  'chat.skill_picker.title': '技能',
  'chat.skill_picker.hint': '↑↓ 瀏覽 · Enter 選擇 · Esc 關閉',
};

export default dict;
