'use client';

import { Button, type ButtonProps } from './Button';
import { trackEvent, type AnalyticsEvent } from '@/lib/analytics';

type EventPayload = Record<string, string | number | boolean>;

interface TrackedButtonProps extends ButtonProps {
  event: AnalyticsEvent;
  eventPayload?: EventPayload;
}

export function TrackedButton({
  event,
  eventPayload,
  onClick,
  ...rest
}: TrackedButtonProps) {
  return (
    <Button
      {...rest}
      onClick={(e) => {
        trackEvent(event, eventPayload);
        onClick?.(e);
      }}
    />
  );
}
