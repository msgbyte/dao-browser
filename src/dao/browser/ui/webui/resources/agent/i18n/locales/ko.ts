// Auto-translated by scripts/i18n-translate.py.
// Lang: ko. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': '예상 컨텍스트: {tokens} 토큰',
  'chat.gauge.tooltip_with_capacity': '예상 컨텍스트: {tokens} 토큰 / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': '요약 취소',
  'chat.compact.start_tooltip': '기록을 단일 컨텍스트 메시지로 요약',
  'chat.compact.compacting': '압축 중…',
  'chat.compact.label': '압축',
  'chat.compact.cancelled': '압축 취소됨',
  'chat.compact.success': '{count}개의 메시지를 1개의 요약으로 압축',
  'chat.compact.failed': '압축 실패: {error}',
  'chat.empty_guide.title': '어떻게 도와드릴까요?',
  'chat.empty_guide.hint': '현재 페이지에 대해 질문하거나 선택 항목을 요약하거나 스킬을 실행하세요.',
  'chat.attach.page.dismiss_title': '이 페이지를 첨부하지 않음',
  'chat.attach.selection.label': '선택',
  'chat.attach.selection.dismiss_title': '이 선택 항목을 첨부하지 않음',
  'chat.message_actions.copy_tooltip': '답변 텍스트 복사',
  'chat.message_actions.share_tooltip': '이미지로 복사',
  'chat.message_actions.regenerate_tooltip': '응답 재생성',
  'chat.message_actions.empty': '비어 있음',
  'chat.message_actions.copied': '복사됨',
  'chat.message_actions.shared': '공유됨',
  'chat.message_actions.failed': '실패',
  'chat.toast.wait_for_turn': '현재 차례가 끝날 때까지 기다리세요',
  'chat.skill_picker.aria_label': '스킬 선택기',
  'chat.skill_picker.title': '스킬',
  'chat.skill_picker.hint': '↑↓로 탐색 · Enter로 선택 · Esc로 닫기',
};

export default dict;
