// Auto-translated by scripts/i18n-translate.py.
// Lang: ca. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Context estimat: {tokens} tokens',
  'chat.gauge.tooltip_with_capacity': 'Context estimat: {tokens} tokens / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Cancel·la el resum',
  'chat.compact.start_tooltip': 'Resumeix l\'historial en un únic missatge de context',
  'chat.compact.compacting': 'Compactant…',
  'chat.compact.label': 'Compacta',
  'chat.compact.cancelled': 'Compactació cancel·lada',
  'chat.compact.success': '{count} missatges compactats → 1 resum',
  'chat.compact.failed': 'Error en la compactació: {error}',
  'chat.empty_guide.title': 'Com et puc ajudar?',
  'chat.empty_guide.hint': 'Pregunta sobre la pàgina actual, resumeix seleccions o executa una Skill.',
  'chat.attach.page.dismiss_title': 'No adjuntis aquesta pàgina',
  'chat.attach.selection.label': 'selecció',
  'chat.attach.selection.dismiss_title': 'No adjuntis aquesta selecció',
  'chat.message_actions.copy_tooltip': 'Copia el text de la resposta',
  'chat.message_actions.share_tooltip': 'Copia com a imatge',
  'chat.message_actions.regenerate_tooltip': 'Regenera la resposta',
  'chat.message_actions.empty': 'Buit',
  'chat.message_actions.copied': 'Copiat',
  'chat.message_actions.shared': 'Compartit',
  'chat.message_actions.failed': 'Error',
  'chat.toast.wait_for_turn': 'Espera que acabi el torn actual',
  'chat.skill_picker.aria_label': 'Selector de Skills',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint': '↑↓ per navegar · Enter per seleccionar · Esc per descartar',
};

export default dict;
