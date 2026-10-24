import { GITHUB_URL } from '@/lib/version';

export function InlineMarkdown({ text }: { text: string }) {
  // The changelog uses single-line bullets with inline code and links only.
  return text.split(/(`[^`]+`|\[[^\]]+\]\([^)]+\))/g).map((part, index) => {
    if (part.startsWith('`') && part.endsWith('`')) {
      return <code key={index}>{part.slice(1, -1)}</code>;
    }
    const link = /^\[([^\]]+)\]\(([^)]+)\)$/.exec(part);
    if (link) {
      try {
        const url = new URL(link[2], `${GITHUB_URL}/blob/main/docs/changelog/`);
        if (url.protocol === 'https:' || url.protocol === 'http:') {
          return <a key={index} href={url.href}>{link[1]}</a>;
        }
      } catch {
        // Leave malformed links readable as source text.
      }
    }
    return part;
  });
}
