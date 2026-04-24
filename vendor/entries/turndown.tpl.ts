// Minified `turndown` IIFE. The bundle declares
// `var __VendorModule = (()=>{...})();` at top level. We wrap it inside
// a factory IIFE that returns the namespace, so it can be composed with
// other bundles (notably `readability`) without colliding on the global
// `__VendorModule` symbol.
//
// Consumer (dao_page_capture.ts) builds a combined script:
//
//   (function() {
//     var R = READABILITY_BUNDLE_IIFE;            // { Readability }
//     var T = TURNDOWN_BUNDLE_IIFE;                // { TurndownService }
//     var article = new R.Readability(document.cloneNode(true)).parse();
//     var ts = new T.TurndownService({...});
//     ts.remove(['script','style']);              // drop script/style blocks
//     ts.addRule('img-alt-only', {
//       filter: 'img',
//       replacement: (_c, n) => '[image: ' + (n.getAttribute('alt')||'') + ']',
//     });
//     return JSON.stringify({
//       url: location.href,
//       title: article ? article.title : document.title,
//       markdown: article ? ts.turndown(article.content) : document.body.innerText,
//     });
//   })();
//
// This file only provides the self-executing namespace.
export const TURNDOWN_BUNDLE_IIFE =
    `(function(){__BUNDLE__;return __VendorModule;})()`;
