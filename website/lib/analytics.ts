export const TIANJI_WEBSITE_ID = 'cmphf1o19dv4k78rcxz79qz3x';

export type AnalyticsEvent =
  | 'download_click'
  | 'download_redirect'
  | 'github_click'
  | 'nav_link_click';

type EventPayload = Record<string, string | number | boolean>;

interface TianjiTracker {
  track: (name: string, payload?: EventPayload) => void;
}

declare global {
  interface Window {
    tianji?: TianjiTracker;
  }
}

export function trackEvent(name: AnalyticsEvent, payload?: EventPayload): void {
  if (typeof window === 'undefined') return;
  const tracker = window.tianji;
  if (!tracker || typeof tracker.track !== 'function') return;
  try {
    tracker.track(name, payload);
  } catch {
    // Swallow tracker errors — analytics must never break the UI.
  }
}
