'use client';

import { useEffect, useState } from 'react';
import { LucideIcon } from '@/components/ui/LucideIcon';
import styles from './download.module.css';

interface PlatformInfo {
  label: string;
  url: string;
}

interface DownloadConfig {
  version: string;
  chromiumVersion: string;
  releasedAt: string;
  platforms: Record<string, PlatformInfo>;
  /** Key into `platforms`; the default platform we redirect to. */
  default: string;
}

type Status = 'loading' | 'ready' | 'error';

/**
 * Detect the user's preferred platform from the User-Agent. We deliberately
 * keep this conservative — when in doubt, fall back to the JSON's `default`.
 */
function detectPlatformKey(config: DownloadConfig): string {
  if (typeof navigator === 'undefined') return config.default;
  const ua = navigator.userAgent.toLowerCase();
  if (ua.includes('mac')) return 'macArm64';
  // Future platforms (linux, win) can be wired here when we ship them.
  return config.default;
}

export function DownloadRedirect() {
  const [status, setStatus] = useState<Status>('loading');
  const [config, setConfig] = useState<DownloadConfig | null>(null);
  const [targetUrl, setTargetUrl] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    (async () => {
      try {
        const res = await fetch('/info.json', { cache: 'no-store' });
        if (!res.ok) {
          throw new Error(`Failed to load info.json (HTTP ${res.status})`);
        }
        const cfg = (await res.json()) as DownloadConfig;
        if (cancelled) return;

        const key = detectPlatformKey(cfg);
        const target = cfg.platforms[key]?.url ?? cfg.platforms[cfg.default]?.url;
        if (!target) {
          throw new Error('No download URL configured for this platform.');
        }

        setConfig(cfg);
        setTargetUrl(target);
        setStatus('ready');

        // Redirect on the next tick so the "preparing your download" message
        // is rendered before navigation. `replace` keeps the back button sane.
        window.setTimeout(() => {
          window.location.replace(target);
        }, 200);
      } catch (e) {
        if (cancelled) return;
        setErrorMessage(e instanceof Error ? e.message : String(e));
        setStatus('error');
      }
    })();

    return () => {
      cancelled = true;
    };
  }, []);

  return (
    <main className={styles.root}>
      <div className={styles.card}>
        {status === 'loading' && (
          <>
            <div className={`${styles.iconWrap} ${styles.iconWrapLoading}`}>
              <LucideIcon
                name="loader-circle"
                size={36}
                className={styles.spin}
                aria-hidden
              />
            </div>
            <h1 className={styles.heading}>Preparing your download…</h1>
            <p className={styles.body}>
              Fetching the latest release. You&apos;ll be redirected in a moment.
            </p>
          </>
        )}

        {status === 'ready' && config && targetUrl && (
          <>
            <div className={`${styles.iconWrap} ${styles.iconWrapReady}`}>
              <LucideIcon name="circle-check" size={40} aria-hidden />
            </div>
            <h1 className={styles.heading}>Your download is starting</h1>
            <span className={styles.platformChip}>
              <LucideIcon name="download" size={14} aria-hidden />
              Dao Browser v{config.version} ·{' '}
              {config.platforms[detectPlatformKey(config)]?.label ??
                config.platforms[config.default].label}
            </span>
            <p className={styles.body}>
              If nothing happens,{' '}
              <a href={targetUrl} className={styles.manualLink}>
                <LucideIcon name="download" size={14} aria-hidden />
                click here to download manually
                <LucideIcon name="arrow-right" size={14} aria-hidden />
              </a>
            </p>
          </>
        )}

        {status === 'error' && (
          <>
            <div className={`${styles.iconWrap} ${styles.iconWrapError}`}>
              <LucideIcon name="circle-alert" size={40} aria-hidden />
            </div>
            <h1 className={styles.heading}>We couldn&apos;t prepare your download</h1>
            <p className={styles.body}>
              {errorMessage ?? 'Something went wrong.'}
            </p>
            <p className={styles.body}>
              Please head to{' '}
              <a
                className={styles.manualLink}
                href="https://github.com/moonrailgun/dao-browser/releases"
                target="_blank"
                rel="noopener noreferrer"
              >
                <LucideIcon name="star" size={14} aria-hidden />
                the GitHub Releases page
                <LucideIcon name="arrow-right" size={14} aria-hidden />
              </a>{' '}
              and grab the latest build.
            </p>
          </>
        )}
      </div>
    </main>
  );
}
