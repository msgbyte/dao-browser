// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/dao_webstore_branding_tab_helper.h"

#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr char kBrandingScript[] = R"js(
(function() {
  if (window.__daoBrandingApplied) return;
  window.__daoBrandingApplied = true;

  const brandRe = /\bChrom(?:e|ium)\b/g;
  const brandTo = 'Dao';

  function isButtonContext(node) {
    let el = node.nodeType === Node.TEXT_NODE ? node.parentElement : node;
    while (el) {
      const tag = el.tagName;
      if (tag === 'BUTTON' || el.getAttribute('role') === 'button' ||
          tag === 'A' && el.classList.contains('button')) {
        return true;
      }
      if (tag === 'BODY') break;
      el = el.parentElement;
    }
    return false;
  }

  function replaceInNode(node) {
    if (node.nodeType === Node.TEXT_NODE) {
      if (isButtonContext(node) && brandRe.test(node.textContent)) {
        brandRe.lastIndex = 0;
        node.textContent = node.textContent.replace(brandRe, brandTo);
      }
      return;
    }
    if (node.nodeType === Node.ELEMENT_NODE) {
      for (const child of node.childNodes) {
        replaceInNode(child);
      }
      if (isButtonContext(node)) {
        for (const attr of ['aria-label', 'title']) {
          const val = node.getAttribute(attr);
          if (val && brandRe.test(val)) {
            brandRe.lastIndex = 0;
            node.setAttribute(attr, val.replace(brandRe, brandTo));
          }
        }
      }
    }
  }

  if (document.body) {
    replaceInNode(document.body);
  }

  const observer = new MutationObserver(mutations => {
    for (const m of mutations) {
      for (const node of m.addedNodes) {
        replaceInNode(node);
      }
      if (m.type === 'characterData' && m.target.nodeType === Node.TEXT_NODE) {
        replaceInNode(m.target);
      }
    }
  });

  observer.observe(document.body || document.documentElement, {
    childList: true,
    subtree: true,
    characterData: true,
  });
})();
)js";

void ExecuteBrandingScript(content::WebContents* contents) {
  if (!contents) {
    return;
  }

  content::RenderFrameHost* frame = contents->GetPrimaryMainFrame();
  if (!frame) {
    return;
  }

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kBrandingScript)), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

}  // namespace

DaoWebstoreBrandingTabHelper::DaoWebstoreBrandingTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoWebstoreBrandingTabHelper>(
          *web_contents) {}

DaoWebstoreBrandingTabHelper::~DaoWebstoreBrandingTabHelper() = default;

void DaoWebstoreBrandingTabHelper::DocumentOnLoadCompletedInPrimaryMainFrame() {
  MaybeInjectBrandingScript();
}

void DaoWebstoreBrandingTabHelper::InjectBrandingScriptForTesting(
    content::WebContents* contents) {
  ExecuteBrandingScript(contents);
}

void DaoWebstoreBrandingTabHelper::MaybeInjectBrandingScript() {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }

  const GURL& url = contents->GetLastCommittedURL();
  if (!url.is_valid() || url.host() != "chromewebstore.google.com") {
    return;
  }

  ExecuteBrandingScript(contents);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoWebstoreBrandingTabHelper);

}  // namespace dao
