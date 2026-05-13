// Auto-translated by scripts/i18n-translate.py.
// Lang: fr-CA. Hand-edit this file once and the script will skip
// it next time (use --force to overwrite).
import en from './en.js';
import type { Dictionary } from '../i18n.js';

const dict: Dictionary = {
  ...en,
  'chat.gauge.tooltip_no_capacity': 'Contexte estimé : {tokens} jetons',
  'chat.gauge.tooltip_with_capacity': 'Contexte estimé : {tokens} jetons / {capacity} ({percent}%)',
  'chat.compact.cancel_tooltip': 'Annuler la synthèse',
  'chat.compact.start_tooltip': 'Résumer l\'historique en un seul message de contexte',
  'chat.compact.compacting': 'Compression…',
  'chat.compact.label': 'Compacter',
  'chat.compact.cancelled': 'Compression annulée',
  'chat.compact.success': 'Compaction de {count} messages → 1 résumé',
  'chat.compact.failed': 'Échec de la compaction : {error}',
  'chat.empty_guide.title': 'Comment puis-je aider?',
  'chat.empty_guide.hint': 'Demandez à propos de la page actuelle, résumez des sélections ou exécutez une Skill.',
  'chat.attach.page.dismiss_title': 'Ne pas attacher cette page',
  'chat.attach.selection.label': 'sélection',
  'chat.attach.selection.dismiss_title': 'Ne pas attacher cette sélection',
  'chat.message_actions.copy_tooltip': 'Copier le texte de la réponse',
  'chat.message_actions.share_tooltip': 'Copier comme image',
  'chat.message_actions.regenerate_tooltip': 'Régénérer la réponse',
  'chat.message_actions.empty': 'Vide',
  'chat.message_actions.copied': 'Copié',
  'chat.message_actions.shared': 'Partagé',
  'chat.message_actions.failed': 'Échoué',
  'chat.toast.wait_for_turn': 'Attendez la fin du tour actuel',
  'chat.skill_picker.aria_label': 'Sélecteur de Skill',
  'chat.skill_picker.title': 'Skills',
  'chat.skill_picker.hint': '↑↓ pour naviguer · Entrée pour sélectionner · Échap pour annuler',
};

export default dict;
