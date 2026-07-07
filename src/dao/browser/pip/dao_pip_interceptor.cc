// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_interceptor.h"

#include <optional>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/dao_pref_names.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/pip/dao_pip_site_rules.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Main-world PiP override template. The first %s is the enabled boolean
// literal, the second %s is the CSS selector JS string literal, and the third
// %s is the custom styles JS array literal.
constexpr char kPipOverrideMainWorldTemplate[] = R"js(
(function() {
  var DAO_PIP_ENABLED_ATTR = 'data-dao-enhanced-pip-enabled';
  window.__daoEnhancedPipEnabled = %s;
  if (document.documentElement) {
    document.documentElement.setAttribute(
        DAO_PIP_ENABLED_ATTR, window.__daoEnhancedPipEnabled ? 'true' : 'false');
  }
  if (window.__daoPipOverrideInstalled) return;
  window.__daoPipOverrideInstalled = true;
  window.__daoDocumentPipOpenPromise = null;

  var DAO_PIP_SELECTOR = %s;
  var DAO_PIP_CUSTOM_STYLES = %s;
  var DAO_PIP_TARGET_ATTR = 'data-dao-pip-target';
  var origRequestPiP = HTMLVideoElement.prototype.requestPictureInPicture;

  function isEnhancedPipEnabled() {
    var root = document.documentElement;
    var attr = root ? root.getAttribute(DAO_PIP_ENABLED_ATTR) : null;
    if (attr === 'false') return false;
    if (attr === 'true') return true;
    return window.__daoEnhancedPipEnabled !== false;
  }

  function dispatchVideoPipEvent(video, type, pipWindow) {
    if (!video) return;

    var event = null;
    try {
      if (window.PictureInPictureEvent) {
        event = new PictureInPictureEvent(type, {
          pictureInPictureWindow: pipWindow,
        });
      }
    } catch(e) {
      event = null;
    }

    if (!event) {
      event = new Event(type);
    }

    try {
      if (!('pictureInPictureWindow' in event)) {
        Object.defineProperty(event, 'pictureInPictureWindow', {
          configurable: true,
          enumerable: true,
          get: function() {
            return pipWindow;
          },
        });
      }
    } catch(e) {}

    video.dispatchEvent(event);
  }

  function getElementName(el) {
    if (!el) return '';
    var className = '';
    try {
      className = typeof el.className === 'string' ? el.className : '';
    } catch(e) {}
    return ((el.id || '') + ' ' + className).toLowerCase();
  }

  function getRect(el) {
    try {
      return el.getBoundingClientRect();
    } catch(e) {
      return null;
    }
  }

  function getRectArea(el) {
    var rect = getRect(el);
    if (!rect) return 0;
    return Math.max(0, rect.width) * Math.max(0, rect.height);
  }

  function isUsableVideo(video) {
    if (!video) return false;
    var rect = getRect(video);
    if (!rect) return true;
    return rect.width > 0 && rect.height > 0;
  }

  function findFallbackVideo() {
    var videos = Array.from(document.querySelectorAll('video'))
        .filter(isUsableVideo);
    videos.sort(function(a, b) {
      var bScore = (b.videoWidth || 0) * (b.videoHeight || 0) ||
          getRectArea(b);
      var aScore = (a.videoWidth || 0) * (a.videoHeight || 0) ||
          getRectArea(a);
      return bScore - aScore;
    });
    return videos[0] || null;
  }

  function isReasonablePipTarget(el, videoRect) {
    if (!el || el === document.body || el === document.documentElement) {
      return false;
    }
    var rect = getRect(el);
    if (!rect || rect.width < 120 || rect.height < 80) {
      return false;
    }
    if (videoRect &&
        (rect.width < videoRect.width * 0.5 ||
         rect.height < videoRect.height * 0.5)) {
      return false;
    }
    return true;
  }

  function findFallbackTarget(video) {
    if (!video) return null;
    var videoRect = getRect(video);
    var best = null;
    for (var el = video.parentElement;
         el && el !== document.body && el !== document.documentElement;
         el = el.parentElement) {
      if (!isReasonablePipTarget(el, videoRect)) {
        continue;
      }
      if (!best) {
        best = el;
      }
      if (/player|bpx|bili|video/.test(getElementName(el))) {
        best = el;
      }
    }
    return best || video;
  }

  async function daoDocumentPip(videoEl) {
    if (!window.documentPictureInPicture) {
      return null;
    }

    var target = document.querySelector(DAO_PIP_SELECTOR);
    var video = videoEl ||
        (target && target.matches('video') ? target :
         target && target.querySelector('video')) ||
        findFallbackVideo();
    if (!target) {
      target = findFallbackTarget(video);
    }
    if (!target) {
      return null;
    }

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
      '[' + DAO_PIP_TARGET_ATTR + '="true"]' +
      '{width:100vw!important;height:100vh!important;' +
      'position:fixed!important;top:0!important;left:0!important}' +
      '[' + DAO_PIP_TARGET_ATTR + '="true"] video' +
      '{width:100%%!important;height:100%%!important;' +
      'object-fit:contain!important}';
    pipWindow.document.head.appendChild(pipStyle);

    if (DAO_PIP_CUSTOM_STYLES.length) {
      var customStyle = document.createElement('style');
      customStyle.textContent = DAO_PIP_CUSTOM_STYLES.join('\n');
      pipWindow.document.head.appendChild(customStyle);
    }

    var originalParent = target.parentElement;
    var originalNextSibling = target.nextSibling;
    var hadTargetAttr = target.hasAttribute(DAO_PIP_TARGET_ATTR);
    var originalTargetAttr = target.getAttribute(DAO_PIP_TARGET_ATTR);
    var shouldDispatchVideoPipEvents = !!videoEl;
    target.setAttribute(DAO_PIP_TARGET_ATTR, 'true');
    pipWindow.document.body.appendChild(target);
    if (shouldDispatchVideoPipEvents) {
      dispatchVideoPipEvent(video, 'enterpictureinpicture', pipWindow);
    }

    function preserveKeyboardEventValue(event, name, value) {
      try {
        Object.defineProperty(event, name, {
          configurable: true,
          enumerable: true,
          get: function() {
            return value;
          },
        });
      } catch(e) {}
    }

    function cloneForwardedEvent(e) {
      if (e.type === 'keydown' || e.type === 'keyup' ||
          e.type === 'keypress') {
        var init = {
          bubbles: e.bubbles,
          cancelable: e.cancelable,
          composed: e.composed,
          key: e.key,
          code: e.code,
          location: e.location,
          repeat: e.repeat,
          isComposing: e.isComposing,
          ctrlKey: e.ctrlKey,
          shiftKey: e.shiftKey,
          altKey: e.altKey,
          metaKey: e.metaKey,
          keyCode: e.keyCode,
          charCode: e.charCode,
          which: e.which,
        };
        var keyboardEvent = new KeyboardEvent(e.type, init);
        preserveKeyboardEventValue(keyboardEvent, 'keyCode', e.keyCode);
        preserveKeyboardEventValue(keyboardEvent, 'charCode', e.charCode);
        preserveKeyboardEventValue(keyboardEvent, 'which', e.which);
        return keyboardEvent;
      }
      return new e.constructor(e.type, e);
    }

    function isBilibiliPipRule() {
      return DAO_PIP_SELECTOR === '#bilibili-player .bpx-player-container';
    }

    function adjustBilibiliVolumeKey(e) {
      if (!isBilibiliPipRule() || e.type !== 'keydown' ||
          (e.key !== 'ArrowUp' && e.key !== 'ArrowDown')) {
        return false;
      }

      var media = video || (target && target.querySelector &&
          target.querySelector('video'));
      var player = window.player;
      var hasPlayerVolume = player &&
          typeof player.getVolume === 'function' &&
          typeof player.setVolume === 'function';
      var volume = Number(hasPlayerVolume ? player.getVolume() :
          (media ? media.volume : NaN));
      if (Number.isFinite(volume)) {
        volume = Math.max(0, Math.min(1, volume +
            (e.key === 'ArrowUp' ? 0.1 : -0.1)));
        if (hasPlayerVolume) {
          player.setVolume(volume);
        } else if (media) {
          media.volume = volume;
        }
        if (e.key === 'ArrowUp' && media && media.muted && volume > 0) {
          media.muted = false;
        }
        return true;
      }

      try {
        // Bilibili exposes player APIs in the page main world.
        var script = document.createElement('script');
        var delta = e.key === 'ArrowUp' ? '0.1' : '-0.1';
        script.textContent =
            '(function(){try{' +
            'var player=window.player;' +
            'if(!player||typeof player.getVolume!=="function"||' +
            'typeof player.setVolume!=="function")return;' +
            'var volume=Number(player.getVolume());' +
            'if(!Number.isFinite(volume))return;' +
            'player.setVolume(Math.max(0,Math.min(1,volume+' + delta + ')));' +
            '}catch(e){}})();';
        (document.documentElement || document.head || document.body)
            .appendChild(script);
        script.remove();
        return true;
      } catch(e) {
        return false;
      }
    }

    var eventsToForward = [
      'keydown', 'keyup', 'keypress',
      'mousemove', 'mouseup', 'mousedown',
      'pointermove', 'pointerup', 'pointerdown',
      'click'
    ];
    eventsToForward.forEach(function(type) {
      pipWindow.document.addEventListener(type, function(e) {
        if (e.__daoForwarded) return;
        var cloned = cloneForwardedEvent(e);
        cloned.__daoForwarded = true;
        document.dispatchEvent(cloned);
        if (adjustBilibiliVolumeKey(e) && e.cancelable) {
          e.preventDefault();
        }
      }, true);
    });

    var restored = false;
    pipWindow.addEventListener('pagehide', function() {
      if (restored) return;
      restored = true;
      if (originalNextSibling) {
        originalParent.insertBefore(target, originalNextSibling);
      } else {
        originalParent.appendChild(target);
      }
      if (hadTargetAttr) {
        target.setAttribute(DAO_PIP_TARGET_ATTR, originalTargetAttr);
      } else {
        target.removeAttribute(DAO_PIP_TARGET_ATTR);
      }
      if (shouldDispatchVideoPipEvents) {
        dispatchVideoPipEvent(video, 'leavepictureinpicture', pipWindow);
      }
    }, {once: true});

    return pipWindow;
  }

  HTMLVideoElement.prototype.requestPictureInPicture = async function() {
    if (!isEnhancedPipEnabled()) {
      return origRequestPiP.call(this);
    }
    try {
      var result = await daoDocumentPip(this);
      if (result) return result;
    } catch(e) {
      console.warn('[Dao] Document PiP failed, falling back:', e);
    }
    return origRequestPiP.call(this);
  };

  window.__daoTriggerDocumentPip = async function() {
    if (!isEnhancedPipEnabled()) {
      return false;
    }
    try {
      if (window.documentPictureInPicture &&
          window.documentPictureInPicture.window) {
        return true;
      }
      if (window.__daoDocumentPipOpenPromise) {
        return !!await window.__daoDocumentPipOpenPromise;
      }
      window.__daoDocumentPipOpenPromise = daoDocumentPip(null)
          .then(function(result) {
            return !!result;
          })
          .finally(function() {
            window.__daoDocumentPipOpenPromise = null;
          });
      return !!await window.__daoDocumentPipOpenPromise;
    } catch(e) {
      window.__daoDocumentPipOpenPromise = null;
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

constexpr int kTriggerResultMaxAttempts = 40;
constexpr base::TimeDelta kTriggerResultPollInterval = base::Milliseconds(50);

constexpr char kTriggerScriptTemplate[] = R"js(
(function() {
  var triggerId = %s;
  var root = document.documentElement;
  if (root) {
    root.setAttribute('data-dao-pip-trigger-id', triggerId);
    root.removeAttribute('data-dao-pip-trigger-result');
  }

  var setResult = function(ok) {
    var root = document.documentElement;
    if (root && root.getAttribute('data-dao-pip-trigger-id') === triggerId) {
      root.setAttribute('data-dao-pip-trigger-result',
                        ok ? 'true' : 'false');
    }
  };

  try {
    var script = document.createElement('script');
    script.textContent = '(' + (async function(triggerId) {
      if (!window.__daoTriggerDocumentPip) {
        return;
      }
      var ok = false;
      try {
        ok = !!await window.__daoTriggerDocumentPip();
      } catch(e) {
        ok = false;
      }
      if (ok) {
        var root = document.documentElement;
        if (root && root.getAttribute('data-dao-pip-trigger-id') === triggerId) {
          root.setAttribute('data-dao-pip-trigger-id', triggerId);
          root.setAttribute('data-dao-pip-trigger-result', 'true');
        }
      }
    }).toString() + ')(' + JSON.stringify(triggerId) + ')';
    (document.documentElement || document.head || document.body)
        .appendChild(script);
    script.remove();
  } catch(e) {}

  setTimeout(async function() {
    var root = document.documentElement;
    if (!root || root.getAttribute('data-dao-pip-trigger-id') !== triggerId ||
        root.getAttribute('data-dao-pip-trigger-result')) {
      return;
    }
    var ok = false;
    try {
      ok = !!(window.__daoTriggerDocumentPip &&
              await window.__daoTriggerDocumentPip());
    } catch(e) {
      ok = false;
    }
    setResult(ok);
  }, 100);

  return triggerId;
})();
)js";

constexpr char kTriggerResultScriptTemplate[] = R"js(
(function() {
  var root = document.documentElement;
  if (!root || root.getAttribute('data-dao-pip-trigger-id') !== %s) {
    return 'pending';
  }
  var result = root.getAttribute('data-dao-pip-trigger-result');
  if (result !== 'true' && result !== 'false') {
    return 'pending';
  }
  return result;
})();
)js";

constexpr char kCloseScript[] = R"js(
(function() {
  var closed = false;
  try {
    closed = !!(window.__daoCloseDocumentPip &&
                window.__daoCloseDocumentPip());
  } catch(e) {}

  try {
    var script = document.createElement('script');
    script.textContent =
        '(function(){try{window.__daoCloseDocumentPip&&' +
        'window.__daoCloseDocumentPip();}catch(e){}})();';
    (document.documentElement || document.head || document.body)
        .appendChild(script);
    script.remove();
  } catch(e) {}

  return closed;
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

std::string BuildCustomStylesArrayLiteral(
    const std::vector<std::string>& custom_styles) {
  std::string literal = "[";
  for (size_t i = 0; i < custom_styles.size(); ++i) {
    if (i > 0) {
      literal.append(",");
    }
    literal.append(EscapeForTemplateLiteral(custom_styles[i]));
  }
  literal.append("]");
  return literal;
}

std::string BuildPipOverrideScript(const PipSiteRule& rule, bool enabled) {
  std::string selector_literal = EscapeForTemplateLiteral(rule.target_selector);
  std::string custom_styles_literal =
      BuildCustomStylesArrayLiteral(rule.custom_styles);
  const char* enabled_literal = enabled ? "true" : "false";
  return base::StringPrintf(
      kPipOverrideMainWorldTemplate, enabled_literal, selector_literal.c_str(),
      custom_styles_literal.c_str());
}

std::string BuildInjectionScript(const PipSiteRule& rule, bool enabled) {
  std::string main_world_js = BuildPipOverrideScript(rule, enabled);
  std::string escaped = EscapeForTemplateLiteral(main_world_js);
  return base::StringPrintf(
      R"js(
(function() {
  if (document.querySelector('#__dao-pip-override')) return;
  try {
    var s = document.createElement('script');
    s.id = '__dao-pip-override';
    s.textContent = %s;
    document.documentElement.appendChild(s);
    s.remove();
  } catch(e) {}
})();
)js",
      escaped.c_str());
}

std::string BuildSetPipOverrideEnabledScript(bool enabled) {
  const char* enabled_literal = enabled ? "true" : "false";
  std::string main_world_js = base::StringPrintf(
      "window.__daoEnhancedPipEnabled = %s;", enabled_literal);
  std::string escaped = EscapeForTemplateLiteral(main_world_js);
  return base::StringPrintf(
      R"js(
(function() {
  window.__daoEnhancedPipEnabled = %s;
  if (document.documentElement) {
    document.documentElement.setAttribute(
        'data-dao-enhanced-pip-enabled', %s ? 'true' : 'false');
  }
  try {
    var s = document.createElement('script');
    s.textContent = %s;
    (document.documentElement || document.head || document.body)
        .appendChild(s);
    s.remove();
  } catch(e) {}
})();
)js",
      enabled_literal, enabled_literal, escaped.c_str());
}

std::string BuildTriggerScript(const std::string& trigger_id) {
  std::string trigger_id_literal = EscapeForTemplateLiteral(trigger_id);
  return base::StringPrintf(kTriggerScriptTemplate,
                            trigger_id_literal.c_str());
}

std::string BuildTriggerResultScript(const std::string& trigger_id) {
  std::string trigger_id_literal = EscapeForTemplateLiteral(trigger_id);
  return base::StringPrintf(kTriggerResultScriptTemplate,
                            trigger_id_literal.c_str());
}

std::optional<bool> ParseTriggerResult(const base::Value& result) {
  if (!result.is_string()) {
    return std::nullopt;
  }

  const std::string& value = result.GetString();
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }

  return std::nullopt;
}

bool IsEnhancedPipEnabled(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile) {
    return true;
  }
  PrefService* prefs = profile->GetPrefs();
  if (!prefs || !prefs->FindPreference(prefs::kDaoEnhancedPipEnabled)) {
    return true;
  }
  return prefs->GetBoolean(prefs::kDaoEnhancedPipEnabled);
}

}  // namespace

DaoPipInterceptor::DaoPipInterceptor(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoPipInterceptor>(*web_contents) {
  auto* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  if (!profile || !profile->GetPrefs() ||
      !profile->GetPrefs()->FindPreference(prefs::kDaoEnhancedPipEnabled)) {
    return;
  }

  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kDaoEnhancedPipEnabled,
      base::BindRepeating(&DaoPipInterceptor::OnEnhancedPipPrefChanged,
                          base::Unretained(this)));
}

DaoPipInterceptor::~DaoPipInterceptor() = default;

// static
bool DaoPipInterceptor::ShouldIntercept(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }
  if (!IsEnhancedPipEnabled(web_contents)) {
    return false;
  }
  return GetPipSiteRule(web_contents->GetLastCommittedURL()).has_value();
}

void DaoPipInterceptor::DocumentOnLoadCompletedInPrimaryMainFrame() {
  MaybeInjectPipOverride();
}

void DaoPipInterceptor::MediaPictureInPictureChanged(
    bool is_picture_in_picture) {
  if (!is_picture_in_picture) {
    const bool was_active = ClearDocumentPipState();
    if (was_active) {
      PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
    }
  }
}

bool DaoPipInterceptor::ClearDocumentPipState() {
  if (!document_pip_active_) {
    return false;
  }
  document_pip_active_ = false;
  capturer_guard_.RunAndReset();
  return true;
}

void DaoPipInterceptor::MaybeInjectPipOverride() {
  auto rule = GetPipSiteRule(web_contents()->GetLastCommittedURL());
  if (!rule) {
    return;
  }

  if (!IsEnhancedPipEnabled(web_contents())) {
    SetPipOverrideEnabled(false);
    return;
  }

  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    return;
  }

  std::string isolated_script = BuildPipOverrideScript(*rule, true);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(isolated_script), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);

  std::string script = BuildInjectionScript(*rule, true);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::SetPipOverrideEnabled(bool enabled) {
  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    return;
  }

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildSetPipOverrideEnabledScript(enabled)),
      base::DoNothing(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::OnEnhancedPipPrefChanged() {
  if (IsEnhancedPipEnabled(web_contents())) {
    MaybeInjectPipOverride();
    return;
  }

  SetPipOverrideEnabled(false);
  if (document_pip_active_) {
    CloseDocumentPip();
  }
}

void DaoPipInterceptor::TriggerDocumentPip(
    base::OnceCallback<void(bool)> callback) {
  if (!ShouldIntercept(web_contents())) {
    SetPipOverrideEnabled(false);
    std::move(callback).Run(false);
    return;
  }

  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    std::move(callback).Run(false);
    return;
  }

  MaybeInjectPipOverride();
  ClearDocumentPipState();
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();

  frame->NotifyUserActivation(
      blink::mojom::UserActivationNotificationType::kInteraction);

  const std::string trigger_id = base::NumberToString(++next_trigger_id_);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildTriggerScript(trigger_id)),
      base::BindOnce(&DaoPipInterceptor::OnTriggerScriptInjected,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     trigger_id),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::OnTriggerScriptInjected(
    base::OnceCallback<void(bool)> callback,
    std::string trigger_id,
    base::Value /*result*/) {
  PollTriggerResult(std::move(callback), std::move(trigger_id),
                    kTriggerResultMaxAttempts);
}

void DaoPipInterceptor::PollTriggerResult(
    base::OnceCallback<void(bool)> callback,
    std::string trigger_id,
    int attempts_remaining) {
  content::RenderFrameHost* frame = web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    std::move(callback).Run(false);
    return;
  }

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildTriggerResultScript(trigger_id)),
      base::BindOnce(&DaoPipInterceptor::OnTriggerResultPoll,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     std::move(trigger_id), attempts_remaining),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoPipInterceptor::OnTriggerResultPoll(
    base::OnceCallback<void(bool)> callback,
    std::string trigger_id,
    int attempts_remaining,
    base::Value result) {
  std::optional<bool> success = ParseTriggerResult(result);
  if (!success && attempts_remaining > 0) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DaoPipInterceptor::PollTriggerResult,
                       weak_factory_.GetWeakPtr(), std::move(callback),
                       std::move(trigger_id), attempts_remaining - 1),
        kTriggerResultPollInterval);
    return;
  }

  if (success.value_or(false)) {
    if (!document_pip_active_) {
      document_pip_active_ = true;
      capturer_guard_ = web_contents()->IncrementCapturerCount(
          gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/true,
          /*is_activity=*/false);
    }
    std::move(callback).Run(true);
    return;
  }

  std::move(callback).Run(false);
}

void DaoPipInterceptor::CloseDocumentPip() {
  ClearDocumentPipState();

  content::RenderFrameHost* frame =
      web_contents()->GetPrimaryMainFrame();
  if (!frame) {
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
    return;
  }

  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string(kCloseScript)), base::DoNothing(),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoPipInterceptor);

}  // namespace dao
