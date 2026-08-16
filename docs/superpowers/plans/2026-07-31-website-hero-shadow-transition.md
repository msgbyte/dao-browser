# Website Hero Shadow Transition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the homepage browser mockup shadow decay naturally without weakening its floating depth.

**Architecture:** Remove clipping from the homepage Hero container and feather the bounded aurora to transparent at its bottom edge. Leave the shared `BrowserFrame` shadow token unchanged.

**Tech Stack:** Next.js 15, React 19, CSS Modules

## Global Constraints

- Modify only the homepage Hero and aurora boundary behavior.
- Preserve the existing `BrowserFrame` shadow, layout spacing, copy, and animation timing.
- Do not run state-changing Git commands without explicit user authorization.

---

### Task 1: Let the Hero shadow overflow and feather the aurora

**Files:**
- Modify: `website/components/Hero.module.css:1-7`
- Modify: `website/app/globals.css:132-149`

**Interfaces:**
- Consumes: The existing `.aurora` element, which clips its animated bloom field.
- Produces: A Hero container that allows the nested `BrowserFrame` shadow to extend beyond its bottom boundary while fading the clipped aurora before that boundary.

- [ ] **Step 1: Record the visual failure**

Confirm at desktop width that the Hero's bottom edge clips the aurora bloom
into a horizontal light band before the first feature section.

- [ ] **Step 2: Apply the minimal CSS change**

Remove the following declaration from `.hero`:

```css
overflow: hidden;
```

Add an alpha mask to `.aurora` that remains fully opaque through 78% of the
hero and fades to transparent at the bottom:

```css
-webkit-mask-image: linear-gradient(
  to bottom,
  #000 0%,
  #000 78%,
  transparent 100%
);
mask-image: linear-gradient(
  to bottom,
  #000 0%,
  #000 78%,
  transparent 100%
);
```

Do not change `--shadow-frame`, Hero padding, or feature-section spacing.

- [ ] **Step 3: Run static verification**

Run:

```bash
cd website
npm run check
npm run build
```

Expected: both commands exit with status 0.

- [ ] **Step 4: Run visual verification**

Inspect `/` at a desktop width near 1820px and a mobile width near 390px.

Expected:

- The mockup retains its existing floating shadow.
- The shadow fades naturally instead of ending in a horizontal band.
- The aurora remains bounded to the Hero.
- The first feature section does not overlap the mockup.
