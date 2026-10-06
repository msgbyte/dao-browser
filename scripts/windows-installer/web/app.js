export function mountInstaller(bridge, locales) {
  const get = id => document.getElementById(id);
  const root = get('installer');
  const directory = get('directory');
  const primary = get('primary');
  let strings = locales.en;
  let state = 'waiting';
  let locked = false;
  let installed = false;
  let editing = false;
  let originalDirectory = '';
  let selectedDirectory = '';
  let logPath = '';
  let error = '';
  let code;
  let tipTimer;

  function render() {
    const busy = state === 'installing' || state === 'closing';
    const invalid = error === 'invalidDirectory';
    root.dataset.state = state;
    root.dataset.installed = String(installed);
    get('headline').textContent = installed ? strings.completeTitle :
      state === 'installing' ? strings.installingTitle :
      state === 'error' ? strings.errorTitle : strings.productName;
    get('version').hidden = state !== 'ready';
    get('description').hidden = state === 'ready';
    get('description').textContent = error ? strings[error] :
      installed ? strings.completeDescription : strings.installingDescription;
    get('directory-option').hidden = busy || installed;
    directory.disabled = busy;
    directory.readOnly = locked;
    directory.setAttribute('aria-invalid', String(invalid));
    get('path-view').hidden = editing;
    get('path-edit').hidden = !editing;
    get('path-text').textContent = selectedDirectory;
    get('path-text').title = selectedDirectory;
    get('directory-hint').textContent = invalid ? strings.invalidDirectory :
      locked ? strings.lockedDirectory : strings.installNote;
    get('directory-hint').classList.toggle('warn', invalid);
    get('change-path').hidden = locked;
    for (const id of ['browse', 'change-path', 'confirm-path']) get(id).disabled = busy || locked || installed;
    get('progress').hidden = state !== 'installing';
    get('tip').hidden = state !== 'installing';
    get('installing-note').hidden = state !== 'installing';
    get('error-details').hidden = !error || invalid || (code === undefined && !logPath);
    get('error-detail-toggle').hidden = get('error-details').hidden;
    get('error-code').hidden = code === undefined;
    get('error-code').textContent = code === undefined ? '' : strings.errorCode.replace('{code}', String(code));
    get('log-detail').hidden = !logPath;
    get('log-path').textContent = logPath;
    get('cancel').disabled = busy;
    get('uninstall').hidden = !locked || installed || !['ready', 'error'].includes(state);
    get('uninstall').disabled = get('uninstall').hidden;
    primary.disabled = busy;
    primary.hidden = state === 'installing';
    primary.textContent = installed ? (error ? strings.retryLaunch : strings.launch) :
      error ? strings.retry : locked ? strings.repair : strings.install;
    if (state === 'installing' && !tipTimer) {
      const tips = [strings.tipSidebar, strings.tipAgent, strings.tipMemory];
      let index = 0;
      get('tip').textContent = tips[index];
      tipTimer = setInterval(() => { get('tip').textContent = tips[++index % tips.length]; }, 3200);
    } else if (state !== 'installing') {
      clearInterval(tipTimer);
      tipTimer = undefined;
    }
  }

  function close() {
    if (!['ready', 'error', 'complete'].includes(state)) return;
    state = 'closing';
    render();
    bridge.postMessage(installed ? 'finish' : 'cancel');
  }

  function openPathEdit() {
    if (locked || installed || !['ready', 'error'].includes(state)) return;
    editing = true;
    directory.value = selectedDirectory;
    render();
    directory.focus();
    directory.select();
  }

  function clearPathError() {
    if (error !== 'invalidDirectory') return;
    error = '';
    code = undefined;
    state = 'ready';
  }

  function commitPath() {
    const target = locked ? originalDirectory : directory.value.trim().replace(/\\+$/, '');
    if (!/^[a-z]:\\[^<>:"|?*\x00-\x1f]+$/i.test(target) || target.length > 180) {
      editing = true;
      error = 'invalidDirectory';
      render();
      directory.focus();
      return false;
    }
    selectedDirectory = target;
    directory.value = target;
    editing = false;
    clearPathError();
    render();
    return true;
  }

  get('install-form').addEventListener('submit', event => {
    event.preventDefault();
    if (!['ready', 'error', 'complete'].includes(state)) return;
    if (installed) {
      state = 'closing';
      render();
      bridge.postMessage('launch');
      return;
    }
    if (!commitPath()) return;
    state = 'installing';
    error = '';
    code = undefined;
    render();
    get('headline').focus();
    bridge.postMessage('install:' + selectedDirectory);
  });
  get('change-path').addEventListener('click', openPathEdit);
  get('confirm-path').addEventListener('click', () => { if (commitPath()) get('change-path').focus(); });
  directory.addEventListener('input', () => {
    if (error === 'invalidDirectory') { clearPathError(); render(); }
  });
  directory.addEventListener('keydown', event => {
    if (event.key === 'Enter') {
      event.preventDefault();
      if (commitPath()) get('change-path').focus();
    }
  });
  get('browse').addEventListener('click', () => {
    if (['ready', 'error'].includes(state) && !locked && !installed) bridge.postMessage('browse:' + directory.value);
  });
  get('cancel').addEventListener('click', close);
  get('uninstall').addEventListener('click', () => {
    if (!locked || installed || !['ready', 'error'].includes(state)) return;
    state = 'closing';
    render();
    bridge.postMessage('uninstall');
  });
  get('minimize').addEventListener('click', () => {
    if (state !== 'waiting' && state !== 'closing') bridge.postMessage('minimize');
  });
  get('error-detail-toggle').addEventListener('click', () => get('error-details').showModal());
  get('close-details').addEventListener('click', () => get('error-details').close());
  root.addEventListener('keydown', event => {
    if (event.key !== 'Escape' || get('error-details').open) return;
    event.preventDefault();
    if (editing) {
      editing = false;
      directory.value = selectedDirectory;
      clearPathError();
      render();
      get('change-path').focus();
    } else close();
  });

  bridge.addEventListener('message', ({data}) => {
    if (!data || typeof data !== 'object') return;
    if (data.type === 'init' && state === 'waiting') {
      const language = data.language === 'zh-CN' ? 'zh-CN' : 'en';
      strings = locales[language];
      document.documentElement.lang = language;
      document.title = strings.windowTitle;
      for (const element of root.querySelectorAll('[data-i18n]')) element.textContent = strings[element.dataset.i18n];
      for (const element of root.querySelectorAll('[data-i18n-label]')) element.setAttribute('aria-label', strings[element.dataset.i18nLabel]);
      originalDirectory = typeof data.directory === 'string' ? data.directory : '';
      selectedDirectory = originalDirectory;
      directory.value = originalDirectory;
      locked = data.locked === true;
      logPath = typeof data.logPath === 'string' ? data.logPath : '';
      get('version').textContent = strings.version.replace('{version}', String(data.version ?? ''));
      state = 'ready';
      root.hidden = false;
      render();
      primary.focus();
    } else if (data.type === 'directory' && typeof data.directory === 'string' &&
               ['ready', 'error'].includes(state) && !locked && !installed) {
      selectedDirectory = data.directory;
      directory.value = data.directory;
      editing = false;
      if (error === 'invalidDirectory') { state = 'ready'; error = ''; code = undefined; }
      render();
      get('change-path').focus();
    } else if (data.type === 'state' && state !== 'waiting' &&
               ['installing', 'complete', 'error'].includes(data.state)) {
      state = data.state;
      if (state === 'complete') installed = true;
      error = state === 'error' ?
        (['invalidDirectory', 'installFailed', 'launchFailed'].includes(data.error) ? data.error : 'installFailed') : '';
      code = state === 'error' && typeof data.code === 'number' ? data.code : undefined;
      if (error === 'invalidDirectory' && !locked) editing = true;
      render();
      if (state === 'complete' || state === 'error') {
        (error === 'invalidDirectory' && !locked ? directory : primary).focus();
      }
    }
  });
  bridge.postMessage('ready');
}
