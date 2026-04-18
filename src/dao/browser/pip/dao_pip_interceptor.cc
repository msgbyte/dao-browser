// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_interceptor.h"

#include "base/functional/callback_helpers.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/pip/dao_pip_site_rules.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Main-world PiP override template. %s is replaced with the CSS selector.
constexpr char kPipOverrideMainWorldTemplate[] = R"js(
(function() {
  if (window.__daoPipOverrideInstalled) return;
  window.__daoPipOverrideInstalled = true;

  var DAO_PIP_SELECTOR = '%s';
  var origRequestPiP = HTMLVideoElement.prototype.requestPictureInPicture;

  async function daoDocumentPip(videoEl) {
    var target = document.querySelector(DAO_PIP_SELECTOR);
    if (!target || !window.documentPictureInPicture) {
      return null;
    }

    var video = videoEl || target.querySelector('video');
    var vw = video ? video.videoWidth : 800;
    var vh = video ? video.videoHeight : 600;
    var aspect = vw / vh || 16 / 9;
    var pipWidth = Math.min(800, vw || 800);
    var pipHeight = Math.round(pipWidth / aspect);

    var pipWindow = await documentPictureInPicture.requestWindow({
      width: pipWidth,
      height: pipHeight,
    });

    Array.from(document.styleSheets).forEach(function(sheet) {
      try {
        if (sheet.href) {
          var link = document.createElement('link');
          link.rel = 'stylesheet';
          link.href = sheet.href;
          pipWindow.document.head.appendChild(link);
        } else if (sheet.cssRules) {
          var style = document.createElement('style');
          pipWindow.document.head.appendChild(style);
          Array.from(sheet.cssRules).forEach(function(rule) {
            style.sheet.insertRule(rule.cssText, style.sheet.cssRules.length);
          });
        }
      } catch(e) {}
    });

    var pipStyle = document.createElement('style');
    pipStyle.textContent =
      'body{margin:0;overflow:hidden;background:#000}' +
      DAO_PIP_SELECTOR.split(' ').pop() +
      '{width:100vw!important;height:100vh!important;' +
      'position:fixed!important;top:0!important;left:0!important}';
    pipWindow.document.head.appendChild(pipStyle);

    var originalParent = target.parentElement;
    var originalNextSibling = target.nextSibling;
    pipWindow.document.body.appendChild(target);

    pipWindow.addEventListener('pagehide', function() {
      if (originalNextSibling) {
        originalParent.insertBefore(target, originalNextSibling);
      } else {
        originalParent.appendChild(target);
      }
    });

    return pipWindow;
  }

  HTMLVideoElement.prototype.requestPictureInPicture = async function() {
    try {
      var result = await daoDocumentPip(this);
      if (result) return result;
    } catch(e) {
      console.warn('[Dao] Document PiP failed, falling back:', e);
    }
    return origRequestPiP.call(this);
  };

  window.__daoTriggerDocumentPip = async function() {
    try {
      var result = await daoDocumentPip(null);
      return !!result;
    } catch(e) {
      console.warn('[Dao] Document PiP trigger failed:', e);
      return false;
    }
  };

  window.__daoCloseDocumentPip = function() {
    if (window.documentPictureInPicture &&
        window.documentPictureInPicture.window) {
      window.documentPictureInPicture.window.close();
      return true;
    }
    return false;
  };
})();
)js";

constexpr char kTriggerScript[] = R"js(
(function() {
  var s = document.createElement('script');
  s.textContent = '(' + (async function() {
    try {
      var ok = await window.__daoTriggerDocumentPip();
      window.__daoPipTriggerResult = ok;
    } catch(e) {
      window.__daoPipTriggerResult = false;
    }
  }).toString() + ')()';
  document.documentElement.appendChild(s);
  s.remove();
})();
)js";

constexpr char kCloseScript[] = R"js(
(function() {
  var s = document.createElement('script');
  s.textContent = 'window.__daoCloseDocumentPip && window.__daoCloseDocumentPip()';
  document.documentElement.appendChild(s);
  s.remove();
})();
)js";

std::string EscapeForTemplateLiteral(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size() + 20);
  escaped.push_back('`');
  for (const char c : input) {
    if (c == '`') {
      escaped.append("\\`");
    } else if (c == '\\') {
      escaped.append("\\\\");
    } else if (c == '$') {
      escaped.append("\\$");
    } else {
      escaped.push_back(c);
    }
  }
  escaped.push_back('`');
  return escaped;
}

std::string BuildInjectionScript(const std::string& selector) {
  std::string main_world_js =
      base::StringPrintf(kPipOverrideMainWorldTemplate, selector.c_str());
  std::string escaped = EscapeForTemplateLiteral(main_world_js);
  return base::StringPrintf(
      R"js(
(function() {
  if (document.querySelector('#__dao-pip-override')) return;
  var s = document.createElement('script');
  s.id = '__dao-pip-override';
  s.textContent = %s;
  document.documentElement.appendChild(s);
  s.remove();
})();
)js",
      escaped.c_str());
}

}  // namespace

DaoPipInterceptor::DaoPipInterceptor(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoPipInterceptor>(*web_contents) {}

DaoPipInterceptor::~DaoPipInterceptor() = default;

// static
bool DaoPipInterceptor::ShouldIntercept(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  return GetPipSiteRule(web_contents->GetLastCommittedURL()).has_value();
}

void DaoPipInterceptor::DocumentOnLoadCompletedInPrimaryMainFrame() {
  MaybeInjectPipOverride();
}

void DaoPipInterceptor::MaybeInjectPipOverride() {
  auto rule = GetPipSiteRule(web_contents()->GetLastCommittedURL());
  if (!rule) {
    return;
  }

  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    return;
  }

  std::string script = BuildInjectionScript(rule->target_selector);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::TriggerDocumentPip(
    base::OnceCallback<void(bool)> callback) {
  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    std::move(callback).Run(false);
    return;
  }

  MaybeInjectPipOverride();

  frame->NotifyUserActivation(
      blink::mojom::UserActivationNotificationType::kInteraction);

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kTriggerScript)),
      base::BindOnce(&DaoPipInterceptor::OnTriggerResult,
                     weak_factory_.GetWeakPtr(), std::move(callback)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::OnTriggerResult(
    base::OnceCallback<void(bool)> callback,
    base::Value result) {
  document_pip_active_ = true;
  capturer_guard_ = web_contents()->IncrementCapturerCount(
      gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/true,
      /*is_activity=*/false);
  std::move(callback).Run(true);
}

void DaoPipInterceptor::CloseDocumentPip() {
  if (!document_pip_active_) {
    return;
  }
  document_pip_active_ = false;
  capturer_guard_.RunAndReset();

  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    return;
  }

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kCloseScript)), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoPipInterceptor);

}  // namespace dao
