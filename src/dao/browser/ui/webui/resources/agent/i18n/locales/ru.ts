// Auto-translated by scripts/i18n-translate.py.
// Lang: ru. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Оценочный контекст: {tokens} токенов',
  'chat.gauge.tooltip_with_capacity': 'Оценочный контекст: {tokens} токенов / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Отменить суммирование',
  'chat.compact.start_tooltip': 'Суммировать историю в одно сообщение контекста',
  'chat.compact.compacting': 'Суммирование…',
  'chat.compact.label': 'Суммировать',
  'chat.compact.cancelled': 'Суммирование отменено',
  'chat.compact.success': 'Суммировано {count} сообщений → 1 итог',
  'chat.compact.failed': 'Ошибка суммирования: {error}',
  'chat.empty_guide.title': 'Чем могу помочь?',
  'chat.empty_guide.hint': 'Спросите о текущей странице, суммируйте выделения или запустите навык.',
  'chat.attach.page.dismiss_title': 'Не прикреплять эту страницу',
  'chat.attach.selection.label': 'выделение',
  'chat.attach.selection.dismiss_title': 'Не прикреплять это выделение',
  'chat.message_actions.copy_tooltip': 'Копировать текст ответа',
  'chat.message_actions.share_tooltip': 'Копировать как изображение',
  'chat.message_actions.regenerate_tooltip': 'Пересоздать ответ',
  'chat.message_actions.empty': 'Пусто',
  'chat.message_actions.copied': 'Скопировано',
  'chat.message_actions.shared': 'Поделено',
  'chat.message_actions.failed': 'Ошибка',
  'chat.toast.wait_for_turn': 'Подождите окончания текущего хода',
  'chat.skill_picker.aria_label': 'Выбор навыка',
  'chat.skill_picker.title': 'Навыки',
  'chat.skill_picker.hint': '↑↓ для навигации · Enter для выбора · Esc для отмены',
};

export default dict;
