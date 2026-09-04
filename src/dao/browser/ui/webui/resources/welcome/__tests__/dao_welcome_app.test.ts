// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, expect, it, vi} from 'vitest';

const strings = vi.hoisted(() => ({
  daoWelcomeTitle: 'Bienvenido a Dao',
  daoWelcomeSubtitle: 'Un navegador pensado para usar el teclado.',
  daoWelcomeNewTab: 'Crear una pestaña en la barra lateral',
  daoWelcomeCloseTab: 'Cerrar la pestaña actual',
  daoWelcomeCommandBar: 'Abrir la barra de comandos',
  daoWelcomeToggleSidebar: 'Mostrar u ocultar la barra lateral',
  daoWelcomeAgentPanel: 'Abrir el panel del asistente de IA',
  daoWelcomeDuplicateTab: 'Duplicar la pestaña actual',
  daoWelcomeCopyUrl: 'Copiar la URL de la página actual',
  daoWelcomeSearchPlaceholder: 'Buscar o introducir una URL...',
  daoWelcomeAgentTitle: 'Asistente de Dao',
  daoCommandBarPlaceholder: 'Introduce una URL o busca...',
  daoWelcomeUrlCopied: 'URL copiada al portapapeles',
  daoWelcomeGithubLink: '¿Te gusta Dao? ¡Danos una estrella!',
  daoImportPageTitle: 'Importar datos del navegador',
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  const shim = await import('../../sidebar/__tests__/lit_test_shim.js');
  return {
    ...shim,
    CrLitElement: class extends shim.CrLitElement {
      disconnectedCallback() {}
    },
  };
});

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {getString: (key: keyof typeof strings) => strings[key]},
}));
vi.mock('../welcome_bridge.js', () => ({markWelcomeShown: vi.fn()}));

afterEach(() => {
  document.body.replaceChildren();
  vi.useRealTimers();
});

it('localizes the page, demo labels, and every tutorial step', async () => {
  vi.useFakeTimers();
  await import('../welcome.js');
  const app = document.createElement('dao-welcome-app') as HTMLElement&{
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(app);
  await app.updateComplete;

  expect(document.title).toBe(strings.daoWelcomeTitle);
  const labels = {
    '.hero h1': strings.daoWelcomeTitle,
    '.hero p': strings.daoWelcomeSubtitle,
    '.sk-search-text': strings.daoWelcomeSearchPlaceholder,
    '.agent-header': strings.daoWelcomeAgentTitle,
    '.command-input-text': strings.daoCommandBarPlaceholder,
    '.toast': strings.daoWelcomeUrlCopied,
    '.github-link': strings.daoWelcomeGithubLink,
    '.migration-link': strings.daoImportPageTitle,
  };
  for (const [selector, text] of Object.entries(labels)) {
    expect(app.shadowRoot!.querySelector(selector)!.textContent!.trim())
        .toBe(text);
  }

  const steps = [
    strings.daoWelcomeNewTab, strings.daoWelcomeCloseTab,
    strings.daoWelcomeCommandBar, strings.daoWelcomeToggleSidebar,
    strings.daoWelcomeAgentPanel, strings.daoWelcomeDuplicateTab,
    strings.daoWelcomeCopyUrl,
  ];
  for (const [index, text] of steps.entries()) {
    app.shadowRoot!.querySelectorAll<HTMLElement>('.dot')[index]!.click();
    await app.updateComplete;
    expect(app.shadowRoot!.querySelector('.annotation-text')!.textContent!.trim())
        .toBe(text);
  }
});
