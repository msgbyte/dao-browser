// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Markdown-to-HTML renderer for Dao Agent chat messages.
// Two-pass parser: block-level structure, then inline formatting.

// ---- HTML Escaping ----

function escapeHtml(text: string): string {
  return text.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
}

// ---- Inline Formatting ----

function renderInline(text: string): string {
  let s = escapeHtml(text);

  // Inline code (must come first to avoid processing inside code spans)
  s = s.replace(/`([^`]+?)`/g, '<code>$1</code>');

  // Links: [text](url)
  s = s.replace(
      /\[([^\]]+)\]\(([^)]+)\)/g,
      '<a href="$2" target="_blank" rel="noopener">$1</a>');

  // Bold + italic: ***text***
  s = s.replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>');

  // Bold: **text**
  s = s.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');

  // Italic: *text*
  s = s.replace(/\*(.+?)\*/g, '<em>$1</em>');

  // Strikethrough: ~~text~~
  s = s.replace(/~~(.+?)~~/g, '<del>$1</del>');

  return s;
}

// ---- Block-level Parsing ----

interface Block {
  type: 'code'|'heading'|'hr'|'blockquote'|'table'|'ulist'|'olist'|
      'paragraph';
  content: string;
  lang?: string;
  level?: number;
  rows?: string[][];
  items?: string[];
}

function parseBlocks(text: string): Block[] {
  const lines = text.split('\n');
  const blocks: Block[] = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i]!;

    // Fenced code block
    const fenceMatch = line.match(/^```(\w*)\s*$/);
    if (fenceMatch) {
      const lang = fenceMatch[1] || '';
      const codeLines: string[] = [];
      i++;
      while (i < lines.length && !lines[i]!.match(/^```\s*$/)) {
        codeLines.push(lines[i]!);
        i++;
      }
      i++;  // skip closing fence
      blocks.push({type: 'code', content: codeLines.join('\n'), lang});
      continue;
    }

    // Heading: # ... ######
    const headingMatch = line.match(/^(#{1,6})\s+(.+)$/);
    if (headingMatch) {
      blocks.push({
        type: 'heading',
        content: headingMatch[2]!,
        level: headingMatch[1]!.length,
      });
      i++;
      continue;
    }

    // Horizontal rule: --- or *** or ___ (3+)
    if (/^(?:---+|\*\*\*+|___+)\s*$/.test(line)) {
      blocks.push({type: 'hr', content: ''});
      i++;
      continue;
    }

    // Table: lines starting with |
    if (line.trim().startsWith('|') && line.trim().endsWith('|')) {
      const tableLines: string[] = [];
      while (i < lines.length && lines[i]!.trim().startsWith('|') &&
             lines[i]!.trim().endsWith('|')) {
        tableLines.push(lines[i]!);
        i++;
      }
      if (tableLines.length >= 2) {
        const rows = tableLines
            .filter(l => !/^\s*\|[\s:-]+\|\s*$/.test(l))  // skip separator
            .map(
                l =>
                    l.trim().replace(/^\||\|$/g, '').split('|').map(
                        c => c.trim()));
        blocks.push({type: 'table', content: '', rows});
      }
      continue;
    }

    // Blockquote: > ...
    if (line.match(/^>\s?/)) {
      const quoteLines: string[] = [];
      while (i < lines.length && lines[i]!.match(/^>\s?/)) {
        quoteLines.push(lines[i]!.replace(/^>\s?/, ''));
        i++;
      }
      blocks.push({type: 'blockquote', content: quoteLines.join('\n')});
      continue;
    }

    // Unordered list: - or * at start
    if (line.match(/^[\s]*[-*]\s+/)) {
      const items: string[] = [];
      while (i < lines.length && lines[i]!.match(/^[\s]*[-*]\s+/)) {
        items.push(lines[i]!.replace(/^[\s]*[-*]\s+/, ''));
        i++;
      }
      blocks.push({type: 'ulist', content: '', items});
      continue;
    }

    // Ordered list: 1. 2. etc
    if (line.match(/^[\s]*\d+\.\s+/)) {
      const items: string[] = [];
      while (i < lines.length && lines[i]!.match(/^[\s]*\d+\.\s+/)) {
        items.push(lines[i]!.replace(/^[\s]*\d+\.\s+/, ''));
        i++;
      }
      blocks.push({type: 'olist', content: '', items});
      continue;
    }

    // Blank line — skip
    if (line.trim() === '') {
      i++;
      continue;
    }

    // Paragraph: collect consecutive non-blank, non-special lines
    const paraLines: string[] = [];
    while (i < lines.length && lines[i]!.trim() !== '' &&
           !lines[i]!.match(/^```/) && !lines[i]!.match(/^#{1,6}\s+/) &&
           !lines[i]!.match(/^(?:---+|\*\*\*+|___+)\s*$/) &&
           !lines[i]!.match(/^>\s?/) && !lines[i]!.match(/^[\s]*[-*]\s+/) &&
           !lines[i]!.match(/^[\s]*\d+\.\s+/) &&
           !(lines[i]!.trim().startsWith('|') &&
             lines[i]!.trim().endsWith('|'))) {
      paraLines.push(lines[i]!);
      i++;
    }
    if (paraLines.length > 0) {
      blocks.push({type: 'paragraph', content: paraLines.join('\n')});
    }
  }

  return blocks;
}

// ---- Render Blocks to HTML ----

function renderBlocks(blocks: Block[]): string {
  return blocks.map(b => {
    switch (b.type) {
      case 'code': {
        const escaped = escapeHtml(b.content);
        const langLabel = b.lang ?
            `<span class="md-code-lang">${escapeHtml(b.lang)}</span>` : '';
        return `<div class="md-code-block">${langLabel}<pre><code>${escaped}</code></pre></div>`;
      }
      case 'heading':
        return `<h${b.level} class="md-heading">${renderInline(b.content)}</h${b.level}>`;
      case 'hr':
        return '<hr class="md-hr">';
      case 'blockquote':
        return `<blockquote class="md-blockquote">${renderMarkdown(b.content)}</blockquote>`;
      case 'table': {
        if (!b.rows || b.rows.length === 0) return '';
        const [header, ...body] = b.rows;
        let html = '<table class="md-table"><thead><tr>';
        for (const cell of header!) {
          html += `<th>${renderInline(cell)}</th>`;
        }
        html += '</tr></thead><tbody>';
        for (const row of body) {
          html += '<tr>';
          for (const cell of row) {
            html += `<td>${renderInline(cell)}</td>`;
          }
          html += '</tr>';
        }
        html += '</tbody></table>';
        return html;
      }
      case 'ulist':
        return '<ul class="md-list">' +
            b.items!.map(it => `<li>${renderInline(it)}</li>`).join('') +
            '</ul>';
      case 'olist':
        return '<ol class="md-list">' +
            b.items!.map(it => `<li>${renderInline(it)}</li>`).join('') +
            '</ol>';
      case 'paragraph': {
        const lines = b.content.split('\n');
        return `<p>${lines.map(l => renderInline(l)).join('<br>')}</p>`;
      }
      default:
        return '';
    }
  }).join('');
}

// ---- Public API ----

export function renderMarkdown(text: string): string {
  if (!text) return '';
  const blocks = parseBlocks(text);
  return renderBlocks(blocks);
}
