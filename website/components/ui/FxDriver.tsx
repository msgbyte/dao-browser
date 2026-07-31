'use client';

import { useEffect } from 'react';

/**
 * Drives the site's FX layer (globals.css):
 *  - `[data-reveal]` elements fade/slide in when scrolled into view.
 *
 * Short-circuits under `prefers-reduced-motion: reduce`, where the CSS
 * already renders the final state. Mount once near the page root.
 */
export function FxDriver() {
  useEffect(() => {
    const prefersReduced = window.matchMedia(
      '(prefers-reduced-motion: reduce)',
    ).matches;

    const revealEls = Array.from(
      document.querySelectorAll<HTMLElement>('[data-reveal]'),
    );

    if (prefersReduced) {
      revealEls.forEach((el) => el.classList.add('is-revealed'));
      return;
    }

    const io = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;
          entry.target.classList.add('is-revealed');
          io.unobserve(entry.target);
        }
      },
      { threshold: 0.2 },
    );

    revealEls.forEach((el) => io.observe(el));
    return () => io.disconnect();
  }, []);

  return null;
}
