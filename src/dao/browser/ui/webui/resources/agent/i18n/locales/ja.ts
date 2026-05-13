// Auto-translated by scripts/i18n-translate.py.
// Lang: ja. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': '推定コンテキスト: {tokens} トークン',
  'chat.gauge.tooltip_with_capacity': '推定コンテキスト: {tokens} トークン / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': '要約をキャンセル',
  'chat.compact.start_tooltip': '履歴を1つのコンテキストメッセージに要約',
  'chat.compact.compacting': '圧縮中…',
  'chat.compact.label': '圧縮',
  'chat.compact.cancelled': '圧縮がキャンセルされました',
  'chat.compact.success': '{count} 件のメッセージを1つの要約に圧縮しました',
  'chat.compact.failed': '圧縮に失敗しました: {error}',
  'chat.empty_guide.title': 'どうお手伝いしましょうか？',
  'chat.empty_guide.hint': '現在のページについて質問する、選択を要約する、またはスキルを実行する。',
  'chat.attach.page.dismiss_title': 'このページを添付しない',
  'chat.attach.selection.label': '選択',
  'chat.attach.selection.dismiss_title': 'この選択を添付しない',
  'chat.message_actions.copy_tooltip': '回答テキストをコピー',
  'chat.message_actions.share_tooltip': '画像としてコピー',
  'chat.message_actions.regenerate_tooltip': '応答を再生成',
  'chat.message_actions.empty': '空',
  'chat.message_actions.copied': 'コピーしました',
  'chat.message_actions.shared': '共有しました',
  'chat.message_actions.failed': '失敗しました',
  'chat.toast.wait_for_turn': '現在のターンが終了するのを待ってください',
  'chat.skill_picker.aria_label': 'スキルピッカー',
  'chat.skill_picker.title': 'スキル',
  'chat.skill_picker.hint': '↑↓で移動 · Enterで選択 · Escで閉じる',
};

export default dict;
