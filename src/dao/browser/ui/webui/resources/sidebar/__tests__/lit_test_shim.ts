// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

type TemplateValue =
    TemplateResult|string|number|boolean|null|undefined|TemplateValue[];

interface TemplateResult {
  strings: TemplateStringsArray;
  values: TemplateValue[];
}

interface EventBinding {
  eventName: string;
  handler: EventListener;
}

interface PropertyBinding {
  propertyName: string;
  value: TemplateValue;
}

function escapeHtml(value: string): string {
  return value.replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;');
}

function isTemplateResult(value: TemplateValue): value is TemplateResult {
  return typeof value === 'object' && value !== null && 'strings' in value &&
      'values' in value;
}

function valueToHtml(
    value: TemplateValue, events: EventBinding[],
    properties: PropertyBinding[]): string {
  if (value === null || value === undefined || value === false) {
    return '';
  }
  if (Array.isArray(value)) {
    return value.map(item => valueToHtml(item, events, properties)).join('');
  }
  if (isTemplateResult(value)) {
    return templateToHtml(value, events, properties);
  }
  return escapeHtml(String(value));
}

function templateToHtml(
    result: TemplateResult, events: EventBinding[],
    properties: PropertyBinding[]): string {
  let markup = '';
  for (let i = 0; i < result.values.length; i++) {
    const value = result.values[i]!;
    const eventMatch = result.strings[i]!.match(/@([a-zA-Z][\w-]*)=$/);
    if (eventMatch) {
      markup += result.strings[i]!.slice(0, -eventMatch[0].length);
      markup += `data-lit-event-${events.length}=""`;
      events.push({
        eventName: eventMatch[1]!,
        handler: value as EventListener,
      });
      continue;
    }
    const propertyMatch =
        result.strings[i]!.match(/\.([a-zA-Z_$][\w$]*)=$/);
    if (propertyMatch) {
      markup += result.strings[i]!.slice(0, -propertyMatch[0].length);
      markup += `data-lit-property-${properties.length}=""`;
      properties.push({
        propertyName: propertyMatch[1]!,
        value,
      });
      continue;
    }
    markup += result.strings[i]!;
    markup += valueToHtml(value, events, properties);
  }
  markup += result.strings[result.strings.length - 1]!;
  return markup;
}

function installPropertyAccessors(ctor: typeof CrLitElement) {
  if (Object.prototype.hasOwnProperty.call(ctor, '__testPropertiesInstalled')) {
    return;
  }
  const properties = ctor.properties || {};
  for (const key of Object.keys(properties)) {
    if (Object.prototype.hasOwnProperty.call(ctor.prototype, key)) {
      continue;
    }
    Object.defineProperty(ctor.prototype, key, {
      configurable: true,
      enumerable: true,
      get() {
        return this[`__${key}`];
      },
      set(value) {
        const oldValue = this[`__${key}`];
        if (Object.is(oldValue, value)) {
          return;
        }
        this[`__${key}`] = value;
        this.requestUpdate(key, oldValue);
      },
    });
  }
  Object.defineProperty(ctor, '__testPropertiesInstalled', {value: true});
}

export class CrLitElement extends HTMLElement {
  static get properties(): Record<string, unknown> {
    return {};
  }

  private updateComplete_: Promise<boolean> = Promise.resolve(true);
  private isUpdatePending_: boolean = false;
  private changedProperties_: Map<PropertyKey, unknown> = new Map();

  constructor() {
    super();
    installPropertyAccessors(this.constructor as typeof CrLitElement);
  }

  get updateComplete(): Promise<boolean> {
    return this.updateComplete_;
  }

  connectedCallback() {
    if (!this.shadowRoot) {
      this.attachShadow({mode: 'open'});
    }
    this.requestUpdate();
  }

  requestUpdate(propertyName?: PropertyKey, oldValue?: unknown) {
    if (propertyName !== undefined &&
        !this.changedProperties_.has(propertyName)) {
      this.changedProperties_.set(propertyName, oldValue);
    }
    if (this.isUpdatePending_) {
      return;
    }
    this.isUpdatePending_ = true;
    this.updateComplete_ = Promise.resolve().then(() => {
      this.performUpdate();
      return true;
    });
  }

  performUpdate() {
    this.isUpdatePending_ = false;
    if (!this.shadowRoot) {
      return;
    }
    const changedProperties = this.changedProperties_;
    this.changedProperties_ = new Map();
    const invokeLifecycleCallbacks =
        (this.constructor as typeof CrLitElement & {
          invokeLifecycleCallbacksForTesting?: boolean;
        }).invokeLifecycleCallbacksForTesting === true;
    if (invokeLifecycleCallbacks) {
      this.willUpdate(changedProperties);
    }
    const events: EventBinding[] = [];
    const properties: PropertyBinding[] = [];
    this.shadowRoot.innerHTML =
        templateToHtml(this.render() as TemplateResult, events, properties);
    for (let i = 0; i < events.length; i++) {
      const binding = events[i]!;
      const node =
          this.shadowRoot.querySelector(`[data-lit-event-${i}]`);
      node?.removeAttribute(`data-lit-event-${i}`);
      node?.addEventListener(binding.eventName, binding.handler);
    }
    for (let i = 0; i < properties.length; i++) {
      const binding = properties[i]!;
      const node = this.shadowRoot.querySelector(
          `[data-lit-property-${i}]`);
      node?.removeAttribute(`data-lit-property-${i}`);
      if (node) {
        Reflect.set(node, binding.propertyName, binding.value);
      }
    }
    if (invokeLifecycleCallbacks) {
      this.updated(changedProperties);
    }
  }

  willUpdate(_changedProperties: Map<PropertyKey, unknown>) {}

  updated(_changedProperties: Map<PropertyKey, unknown>) {}

  render(): TemplateResult {
    return html``;
  }
}

export function html(
    strings: TemplateStringsArray, ...values: TemplateValue[]): TemplateResult {
  return {strings, values};
}

export function css(strings: TemplateStringsArray, ...values: TemplateValue[]) {
  return {strings, values};
}

export const nothing = '';
