// Minified @mozilla/readability IIFE + execution wrappers.
//
// Two exports:
//
// 1. READABILITY_INJECT_SCRIPT — legacy standalone capture script that
//    clones the document, runs Readability, and returns JSON with
//    `textContent` / metadata. Used by agent tools that want plain text
//    only.
//
// 2. READABILITY_BUNDLE_IIFE — raw IIFE that returns the bundle's
//    namespace (`{ Readability }`). Used by dao_page_capture.ts to
//    compose readability + turndown in a single injected script (keeps
//    the HTML form of the article so turndown can convert it to markdown).
const BUNDLE_IIFE = `(function(){__BUNDLE__;return __VendorModule;})()`;

export const READABILITY_INJECT_SCRIPT = `(function() {
  var __M = ${BUNDLE_IIFE};
  var Readability = __M.Readability;
  var clone = document.cloneNode(true);
  var article = new Readability(clone).parse();
  if (!article) return JSON.stringify({error: 'Could not extract readable content'});
  return JSON.stringify({
    title: article.title || '',
    byline: article.byline || '',
    excerpt: article.excerpt || '',
    siteName: article.siteName || '',
    length: article.length || 0,
    textContent: article.textContent || ''
  });
})()`;

export const READABILITY_BUNDLE_IIFE = BUNDLE_IIFE;
