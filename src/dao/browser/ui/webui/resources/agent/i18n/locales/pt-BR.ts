// Auto-translated by scripts/i18n-translate.py.
// Lang: pt-BR. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Contexto estimado: {tokens} tokens',
  'chat.gauge.tooltip_with_capacity': 'Contexto estimado: {tokens} tokens / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Cancelar sumarização',
  'chat.compact.start_tooltip': 'Resumir histórico em uma única mensagem de contexto',
  'chat.compact.compacting': 'Compactando…',
  'chat.compact.label': 'Compactar',
  'chat.compact.cancelled': 'Compactação cancelada',
  'chat.compact.success': '{count} mensagens compactadas → 1 resumo',
  'chat.compact.failed': 'Falha na compactação: {error}',
  'chat.empty_guide.title': 'Como posso ajudar?',
  'chat.empty_guide.hint': 'Pergunte sobre a página atual, resuma seleções ou execute uma Skill.',
  'chat.attach.page.dismiss_title': 'Não anexar esta página',
  'chat.attach.selection.label': 'seleção',
  'chat.attach.selection.dismiss_title': 'Não anexar esta seleção',
  'chat.message_actions.copy_tooltip': 'Copiar texto da resposta',
  'chat.message_actions.share_tooltip': 'Copiar como imagem',
  'chat.message_actions.regenerate_tooltip': 'Regenerar resposta',
  'chat.message_actions.empty': 'Vazio',
  'chat.message_actions.copied': 'Copiado',
  'chat.message_actions.shared': 'Compartilhado',
  'chat.message_actions.failed': 'Falhou',
  'chat.toast.wait_for_turn': 'Aguarde a vez atual terminar',
  'chat.skill_picker.aria_label': 'Seletor de Skill',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint': '↑↓ para navegar · Enter para selecionar · Esc para fechar',
};

export default dict;
