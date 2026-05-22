'use client';

import type { AnchorHTMLAttributes } from 'react';
import { trackEvent, type AnalyticsEvent } from '@/lib/analytics';

type EventPayload = Record<string, string | number | boolean>;

interface TrackedLinkProps extends AnchorHTMLAttributes<HTMLAnchorElement> {
  event: AnalyticsEvent;
  eventPayload?: EventPayload;
}

export function TrackedLink({
  event,
  eventPayload,
  onClick,
  children,
  ...rest
}: TrackedLinkProps) {
  return (
    <a
      {...rest}
      onClick={(e) => {
        trackEvent(event, eventPayload);
        onClick?.(e);
      }}
    >
      {children}
    </a>
  );
}
