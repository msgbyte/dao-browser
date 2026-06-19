// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface FlipMotionSnapshot {
  ids: Set<string>;
  rects: Map<string, DOMRectReadOnly>;
}

export interface FlipMotionOptions {
  duration?: number;
  easing?: string;
  force?: boolean;
  skip?: boolean;
}

const DEFAULT_DURATION_MS = 140;
const DEFAULT_EASING = 'cubic-bezier(0.2, 0, 0, 1)';
const MINIMUM_DELTA_PX = 0.5;

const activeFlipAnimations = new WeakMap<HTMLElement, Animation>();

interface CollectedFlipElement<T extends HTMLElement> {
  element: T;
  id: string;
  bounds: DOMRectReadOnly;
}

interface CollectedFlipElements<T extends HTMLElement> {
  snapshot: FlipMotionSnapshot;
  elements: Array<CollectedFlipElement<T>>;
}

export function snapshotFlipElements<T extends HTMLElement>(
    root: ParentNode|null, selector: string,
    getIdentity: (element: T) => string): FlipMotionSnapshot {
  return collectFlipElements(root, selector, getIdentity).snapshot;
}

export function animateSurvivingFlipElements<T extends HTMLElement>(
    previous: FlipMotionSnapshot|null, root: ParentNode|null, selector: string,
    getIdentity: (element: T) => string,
    options: FlipMotionOptions = {}): Animation[] {
  if (!previous || options.skip || prefersReducedMotion()) {
    return [];
  }
  if (!root) {
    return [];
  }

  const currentIds = collectFlipElementIds(root, selector, getIdentity);
  const removedIdentity =
      [...previous.ids].some(id => !currentIds.has(id));
  if (!options.force && !removedIdentity) {
    return [];
  }

  cancelActiveFlipAnimations(root, selector);
  const current = collectFlipElements(root, selector, getIdentity);
  const animations: Animation[] = [];
  const duration = options.duration ?? DEFAULT_DURATION_MS;
  const easing = options.easing ?? DEFAULT_EASING;

  for (const {element, id, bounds} of current.elements) {
    const oldBounds = previous.rects.get(id);
    if (!oldBounds) {
      continue;
    }

    const deltaX = oldBounds.left - bounds.left;
    const deltaY = oldBounds.top - bounds.top;
    if (Math.abs(deltaX) < MINIMUM_DELTA_PX &&
        Math.abs(deltaY) < MINIMUM_DELTA_PX) {
      continue;
    }

    activeFlipAnimations.get(element)?.cancel();
    const animation = element.animate(
        [
          {transform: `translate(${deltaX}px, ${deltaY}px)`},
          {transform: 'translate(0, 0)'},
        ],
        {duration, easing});
    activeFlipAnimations.set(element, animation);
    void animation.finished.finally(() => {
      if (activeFlipAnimations.get(element) === animation) {
        activeFlipAnimations.delete(element);
      }
    }).catch(() => {});
    animations.push(animation);
  }

  return animations;
}

function collectFlipElementIds<T extends HTMLElement>(
    root: ParentNode, selector: string,
    getIdentity: (element: T) => string): Set<string> {
  const ids = new Set<string>();
  const elements = root.querySelectorAll(selector) as NodeListOf<T>;
  for (const element of elements) {
    const id = getIdentity(element);
    if (!id || ids.has(id)) {
      continue;
    }
    if (!isVisibleFlipElement(element)) {
      continue;
    }
    ids.add(id);
  }
  return ids;
}

function collectFlipElements<T extends HTMLElement>(
    root: ParentNode|null, selector: string,
    getIdentity: (element: T) => string): CollectedFlipElements<T> {
  const ids = new Set<string>();
  const rects = new Map<string, DOMRectReadOnly>();
  const collectedElements: Array<CollectedFlipElement<T>> = [];
  if (!root) {
    return {snapshot: {ids, rects}, elements: collectedElements};
  }

  const elements = root.querySelectorAll(selector) as NodeListOf<T>;
  for (const element of elements) {
    const id = getIdentity(element);
    if (!id || ids.has(id)) {
      continue;
    }
    if (!isVisibleFlipElement(element)) {
      continue;
    }
    const bounds = element.getBoundingClientRect();
    if (bounds.width <= 0 || bounds.height <= 0) {
      continue;
    }
    ids.add(id);
    rects.set(id, bounds);
    collectedElements.push({element, id, bounds});
  }
  return {snapshot: {ids, rects}, elements: collectedElements};
}

function cancelActiveFlipAnimations(
    root: ParentNode, selector: string) {
  const elements =
      root.querySelectorAll(selector) as NodeListOf<HTMLElement>;
  for (const element of elements) {
    const animation = activeFlipAnimations.get(element);
    if (!animation) {
      continue;
    }
    animation.cancel();
    activeFlipAnimations.delete(element);
  }
}

function isVisibleFlipElement(element: HTMLElement): boolean {
  const style = getComputedStyle(element);
  return style.display !== 'none' && style.visibility !== 'hidden';
}

function prefersReducedMotion(): boolean {
  return window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ??
      false;
}
