// Auto-translated by scripts/i18n-translate.py.
// Lang: es. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Contexto estimado: {tokens} tokens',
  'chat.gauge.tooltip_with_capacity': 'Contexto estimado: {tokens} tokens / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Cancelar resumen',
  'chat.compact.start_tooltip': 'Resumir historial en un solo mensaje de contexto',
  'chat.compact.compacting': 'Compactando…',
  'chat.compact.label': 'Compactar',
  'chat.compact.cancelled': 'Compactación cancelada',
  'chat.compact.success': '{count} mensajes compactados → 1 resumen',
  'chat.compact.failed': 'Error al compactar: {error}',
  'chat.empty_guide.title': '¿Cómo puedo ayudar?',
  'chat.empty_guide.hint': 'Pregunta sobre la página actual, resume selecciones o ejecuta una Skill.',
  'chat.attach.page.dismiss_title': 'No adjuntar esta página',
  'chat.attach.selection.label': 'selección',
  'chat.attach.selection.dismiss_title': 'No adjuntar esta selección',
  'chat.message_actions.copy_tooltip': 'Copiar texto de respuesta',
  'chat.message_actions.share_tooltip': 'Copiar como imagen',
  'chat.message_actions.regenerate_tooltip': 'Regenerar respuesta',
  'chat.message_actions.empty': 'Vacío',
  'chat.message_actions.copied': 'Copiado',
  'chat.message_actions.shared': 'Compartido',
  'chat.message_actions.failed': 'Fallido',
  'chat.toast.wait_for_turn': 'Espera a que termine el turno actual',
  'chat.skill_picker.aria_label': 'Selector de Skills',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint': '↑↓ para navegar · Enter para seleccionar · Esc para descartar',
};

export default dict;
