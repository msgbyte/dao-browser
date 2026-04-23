// Minified @mozilla/readability IIFE + execution wrapper.
// Injected into the target page via DevTools Runtime.evaluate to extract
// the main article content.
export const READABILITY_INJECT_SCRIPT = `(function() {
  __BUNDLE__
  var Readability = __VendorModule.Readability;
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
