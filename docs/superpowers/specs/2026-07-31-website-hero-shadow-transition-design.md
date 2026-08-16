# Website Hero Shadow Transition Design

## Problem

The homepage hero's bottom aurora bloom crosses the hero boundary near its
brightest point. Clipping the bloom there produces a wide horizontal light
band before the first feature section.

## Design

- Preserve the existing `BrowserFrame` shadow and floating depth.
- Stop clipping the hero as a whole so the real frame shadow can decay
  naturally beyond the hero boundary.
- Keep the aurora field bounded through its dedicated overflow container, and
  fade its bottom edge to transparent before that boundary.
- Do not change global shadow tokens, feature-section spacing, content, or
  animation timing.

## Verification

- Run the website TypeScript check and production build.
- Visually inspect the homepage transition on desktop and mobile widths.
- Confirm the aurora remains bounded and the mockup shadow has no hard
  horizontal cutoff.
