'use client';

import { useEffect, useRef, useState } from 'react';
import { LucideIcon } from '@/components/ui/LucideIcon';
import { trackEvent } from '@/lib/analytics';
import en from '@/locales/en/download.json';
import zhCN from '@/locales/zh-CN/download.json';
import styles from './download.module.css';

type Status = 'loading' | 'ready' | 'error';

interface PlatformInfo {
  label: string;
  url: string;
  version?: string;
}

interface DownloadConfig {
  version: string;
  platforms: Record<string, PlatformInfo>;
}

function isMacOS(userAgent: string, maxTouchPoints = 0): boolean {
  // iPadOS can use a desktop Mac user agent, but still reports touch support.
  return /Macintosh|Mac OS X/i.test(userAgent) &&
    !/iPhone|iPad|iPod|Android|Mobile/i.test(userAgent) && maxTouchPoints < 2;
}

export function DownloadRedirect() {
  const [status, setStatus] = useState<Status>('loading');
  const [config, setConfig] = useState<DownloadConfig | null>(null);
  const [autoDownload, setAutoDownload] = useState<PlatformInfo | null>(null);
  const [locale, setLocale] = useState('en');
  const redirectTimer = useRef<number | undefined>(undefined);
  const messages = locale === 'zh-CN' ? zhCN : en;

  useEffect(() => {
    let cancelled = false;
    setLocale(navigator.language.toLowerCase().startsWith('zh') ? 'zh-CN' : 'en');

    (async () => {
      try {
        const res = await fetch('/info.json', { cache: 'no-store' });
        if (!res.ok) throw new Error(`Failed to load info.json (HTTP ${res.status})`);
        const cfg = (await res.json()) as DownloadConfig;
        if (cancelled) return;
        if (!Object.values(cfg.platforms).some(platform => platform.url)) {
          throw new Error('No downloads configured');
        }

        const target = isMacOS(navigator.userAgent, navigator.maxTouchPoints)
          ? cfg.platforms.macArm64 : null;
        setConfig(cfg);
        setAutoDownload(target?.url ? target : null);
        setStatus('ready');

        if (target?.url) {
          trackEvent('download_redirect', {
            platform: 'macArm64', version: target.version ?? cfg.version,
          });
          // Let the ready message render before starting the download.
          redirectTimer.current = window.setTimeout(() => {
            window.location.replace(target.url);
          }, 200);
        }
      } catch {
        if (!cancelled) setStatus('error');
      }
    })();

    return () => {
      cancelled = true;
      window.clearTimeout(redirectTimer.current);
    };
  }, []);

  return (
    <main className={styles.root} lang={locale}>
      <div className={styles.card}>
        {status === 'loading' && (
          <>
            <div className={`${styles.iconWrap} ${styles.iconWrapLoading}`}>
              <LucideIcon name="loader-circle" size={36} className={styles.spin} aria-hidden />
            </div>
            <h1 className={styles.heading}>{messages.loadingTitle}</h1>
            <p className={styles.body}>{messages.loadingBody}</p>
          </>
        )}

        {status === 'ready' && config && (
          <>
            <div className={`${styles.iconWrap} ${autoDownload ? styles.iconWrapReady : styles.iconWrapLoading}`}>
              <LucideIcon name={autoDownload ? 'circle-check' : 'download'} size={40} aria-hidden />
            </div>
            <h1 className={styles.heading}>
              {autoDownload ? messages.automaticTitle : messages.title}
            </h1>
            <p className={styles.body}>
              {autoDownload ? messages.automaticBody : messages.manualBody}
            </p>
            <ul className={styles.downloads}>
              {Object.entries(config.platforms).filter(([, platform]) => platform.url).map(([key, platform]) => (
                <li key={key}>
                  <a
                    href={platform.url}
                    className={styles.downloadLink}
                    onClick={() => {
                      window.clearTimeout(redirectTimer.current);
                      trackEvent('download_click', {
                        source: 'download-page', platform: key,
                        version: platform.version ?? config.version,
                      });
                    }}
                  >
                    <LucideIcon name="download" size={16} aria-hidden />
                    {platform.label} · v{platform.version ?? config.version}
                  </a>
                </li>
              ))}
            </ul>
          </>
        )}

        {status === 'error' && (
          <>
            <div className={`${styles.iconWrap} ${styles.iconWrapError}`}>
              <LucideIcon name="circle-alert" size={40} aria-hidden />
            </div>
            <h1 className={styles.heading}>{messages.errorTitle}</h1>
            <p className={styles.body}>{messages.errorBody}</p>
            <a
              className={styles.manualLink}
              href="https://github.com/msgbyte/dao-browser/releases"
              target="_blank"
              rel="noopener noreferrer"
            >
              {messages.releasesLink}
              <LucideIcon name="arrow-right" size={14} aria-hidden />
            </a>
          </>
        )}
      </div>
    </main>
  );
}
