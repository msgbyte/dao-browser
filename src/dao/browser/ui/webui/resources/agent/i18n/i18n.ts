// Lightweight i18n runtime for Dao WebUI surfaces.
//
// Dictionaries live in ./locales/<lang>.ts as plain objects; the active dict
// is selected at module load time from the locale that strings.m.js exposes
// (set by WebUIDataSource::AddString("dao_app_locale", ...) in C++).
//
// Why not loadTimeData / chrome.i18n / a real i18n library?
// - loadTimeData is great when every string is registered in the C++
//   WebUIDataSource (the standard Chromium pattern). We deliberately keep
//   Dao WebUI strings owned by TypeScript: easier to refactor, no Chromium
//   resource ID churn for every label change. We *only* use loadTimeData to
//   read the active locale code.
// - i18next-and-friends would mean another vendor bundle entry. The runtime
//   here is ~30 lines and covers our actual needs (lookup + simple {var}
//   interpolation).
//
// New dictionaries: copy locales/en.ts as a starting point, override only
// the keys you've translated. Untranslated keys fall through to en via the
// `...en` spread in each locale file (see scripts/i18n-bootstrap.ts).

import en from './locales/en.js';

export type Dictionary = Record<string, string>;

const FALLBACK: Dictionary = en;

let active: Dictionary = en;
let activeLocale = 'en';
let initPromise: Promise<void> | null = null;

interface LoadTimeDataLike {
  getString?(id: string): string;
  data?: Record<string, unknown>;
}

function readAppLocaleFromHost(): string {
  // strings.m.js sets window.loadTimeData; it is loaded synchronously before
  // the agent module bundle, so it is present by the time this runs.
  const ltd = (globalThis as { loadTimeData?: LoadTimeDataLike }).loadTimeData;
  try {
    if (ltd?.getString) {
      const v = ltd.getString('dao_app_locale');
      if (typeof v === 'string' && v.length > 0) return v;
    }
    const data = ltd?.data;
    if (data && typeof data['dao_app_locale'] === 'string') {
      return data['dao_app_locale'] as string;
    }
  } catch {
    // valueExists/getString throw if the key is missing — fall through.
  }
  // Last resort. navigator.language can drift from the C++ pak selection,
  // but it is better than always-en when the host injection is missing.
  return navigator.language ?? 'en';
}

// Normalize a locale code into the keys we use under ./locales/.
// 'en-US' -> 'en'; 'zh_CN' -> 'zh-CN'; 'zh-Hans-CN' -> 'zh-CN' (best-effort).
function normalizeLocale(raw: string): string[] {
  const lang = raw.replace(/_/g, '-');
  const candidates: string[] = [];
  candidates.push(lang);

  // 'zh-Hans-CN' -> 'zh-CN'
  const parts = lang.split('-');
  if (parts.length >= 3 && parts[0] && parts[parts.length - 1]) {
    candidates.push(`${parts[0]}-${parts[parts.length - 1]}`);
  }

  // 'en-US' -> 'en'
  if (parts.length >= 2 && parts[0]) candidates.push(parts[0]);

  // Common aliases — Chromium ships 'no' but the world prefers 'nb'; we
  // mirror Chromium's keys so prefer 'no' first if seen.
  if (lang === 'nb') candidates.push('no');
  if (lang === 'he') candidates.push('iw');

  return candidates;
}

async function loadLocale(raw: string): Promise<{ dict: Dictionary; resolved: string }> {
  for (const candidate of normalizeLocale(raw)) {
    if (candidate === 'en') return { dict: en, resolved: 'en' };
    try {
      const mod = (await import(`./locales/${candidate}.js`)) as { default: Dictionary };
      return { dict: mod.default, resolved: candidate };
    } catch {
      // try next candidate
    }
  }
  return { dict: en, resolved: 'en' };
}

// Public: resolve the active dictionary. Idempotent — first call kicks off
// the import; subsequent calls await the same promise.
export function initI18n(): Promise<void> {
  if (initPromise) return initPromise;
  initPromise = (async () => {
    const raw = readAppLocaleFromHost();
    const { dict, resolved } = await loadLocale(raw);
    active = dict;
    activeLocale = resolved;
  })();
  return initPromise;
}

export function currentLocale(): string {
  return activeLocale;
}

// Public: look up a translated string. Vars are substituted via {name} tokens.
// Missing keys fall back to en, then to the literal key (so a missing
// translation surfaces visibly as a key during development rather than as
// an empty string).
export function t(key: string, vars?: Record<string, string | number>): string {
  let s = active[key] ?? FALLBACK[key] ?? key;
  if (vars) {
    for (const [name, value] of Object.entries(vars)) {
      s = s.replaceAll(`{${name}}`, String(value));
    }
  }
  return s;
}
