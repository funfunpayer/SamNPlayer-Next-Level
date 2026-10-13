// Central UI notices without browser alert() popups: write to the Log tab
// and optionally into a status element. Critical ERROR also shows a short toast.
export function uiLog(level, message) {
  const msg = String(message ?? '');
  if (!msg) return;
  window.dispatchEvent(new CustomEvent('ui:notify', {
    detail: {
      level: level || 'INFO',
      message: msg,
      time: new Date().toLocaleTimeString(),
    },
  }));
}

export function uiError(message, statusEl) {
  const msg = String(message ?? '');
  uiLog('ERROR', msg);
  if (statusEl) statusEl.textContent = msg;
  showToast(msg, 'ERROR');
}

export function uiWarn(message, statusEl) {
  const msg = String(message ?? '');
  uiLog('WARN', msg);
  if (statusEl) statusEl.textContent = msg;
}

export function uiInfo(message, statusEl) {
  const msg = String(message ?? '');
  uiLog('INFO', msg);
  if (statusEl) statusEl.textContent = msg;
}

function ensureToastHost() {
  let host = document.getElementById('ui-toast-host');
  if (host) return host;
  host = document.createElement('div');
  host.id = 'ui-toast-host';
  host.setAttribute('aria-live', 'assertive');
  host.setAttribute('aria-relevant', 'additions');
  document.body.appendChild(host);
  return host;
}

/** Non-blocking toast for critical errors; “Open Log” switches to the Log tab. */
export function showToast(message, level = 'ERROR') {
  const msg = String(message ?? '').trim();
  if (!msg || level !== 'ERROR') return;
  const host = ensureToastHost();
  const el = document.createElement('div');
  el.className = 'ui-toast';
  el.setAttribute('role', 'alert');
  const text = document.createElement('div');
  text.textContent = msg.length > 220 ? msg.slice(0, 217) + '…' : msg;
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.textContent = 'Open Log';
  btn.addEventListener('click', () => {
    document.querySelector('.tab-btn[data-tab="log"]')?.click();
    el.remove();
  });
  el.appendChild(text);
  el.appendChild(btn);
  host.appendChild(el);
  const t = setTimeout(() => el.remove(), 8000);
  el.addEventListener('click', (e) => {
    if (e.target === btn) return;
    clearTimeout(t);
    el.remove();
  });
  while (host.children.length > 3) host.firstElementChild.remove();
}
