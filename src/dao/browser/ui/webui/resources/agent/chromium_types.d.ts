// IDE-only type declarations for Chromium WebUI modules.
// The real resolution is handled by ts_library at GN build time.
// This file is mapped via tsconfig.json paths — do NOT use `declare module`.

declare global {
  namespace chrome {
    function send(msg: string, params?: any[]): void;
    function getVariableValue(name: string): string;
  }
}

export class CrLitElement extends HTMLElement {
  static get properties(): Record<string, any>;
  static get styles(): any;
  readonly $: Record<string, HTMLElement>;
  readonly updateComplete: Promise<boolean>;
  protected render(): unknown;
  protected willUpdate(changedProperties: PropertyValues): void;
  protected updated(changedProperties: PropertyValues): void;
  protected firstUpdated(changedProperties: PropertyValues): void;
  connectedCallback(): void;
  disconnectedCallback(): void;
  requestUpdate(): void;
  fire(eventName: string, detail?: any): void;
  hasUpdated: boolean;
}

export function css(
    strings: TemplateStringsArray, ...values: unknown[]): CSSResultGroup;
export function html(
    strings: TemplateStringsArray, ...values: unknown[]): TemplateResult;
export function render(
    result: TemplateResult, container: HTMLElement): void;
export const nothing: unique symbol;

export type CSSResultGroup = unknown;
export type PropertyValues = Map<string, unknown>;
export type TemplateResult = unknown;
