import { GetLogEntries, ClearLogEntries, OpenLogFolder } from '../wailsjs/go/main/App';
import { EventsOn } from '../wailsjs/runtime/runtime';

// Log tab: selectable, copyable ring of app logs (fixes “can’t copy from status line”).
export function initLog(root) {
  root.innerHTML = `
    <h2>Log</h2>
    <p class="hint">Live messages and errors — select and copy (Ctrl/Cmd+C)
      or “Copy all”. The file under Settings remains the long-term archive.</p>
    <div class="row" style="gap:8px; flex-wrap:wrap; margin-bottom:8px;">
      <label class="hint" style="display:flex; align-items:center; gap:6px;">
        <input type="checkbox" id="log-errors-only" /> Warnings/errors only
      </label>
      <button type="button" id="log-copy">Copy all</button>
      <button type="button" id="log-clear">Clear</button>
      <button type="button" id="log-open-folder">Open folder</button>
      <span class="hint" id="log-status"></span>
    </div>
    <pre id="log-view" class="log-view" tabindex="0"></pre>
  `;

  const el = id => root.querySelector(id);
  const view = el('#log-view');
  let entries = [];

  function lineText(e) {
    const t = e.time || '';
    return `${t}  ${e.level || ''}  ${e.message || ''}`;
  }

  function render() {
    const onlyErr = el('#log-errors-only').checked;
    const lines = entries
      .filter(e => !onlyErr || e.level === 'WARN' || e.level === 'ERROR')
      .map(lineText);
    view.textContent = lines.join('\n');
    view.scrollTop = view.scrollHeight;
  }

  function push(e) {
    entries.push(e);
    if (entries.length > 800) entries = entries.slice(-800);
    render();
  }

  GetLogEntries().then(list => {
    entries = (list || []).map(e => ({
      time: e.time ? new Date(e.time).toLocaleTimeString() + '.' + String(new Date(e.time).getMilliseconds()).padStart(3, '0') : '',
      level: e.level,
      message: e.message,
    }));
    render();
  }).catch(() => {});

  EventsOn('log:line', e => push(e));

  // Frontend-Hinweise (statt alert): errors/Warnings landen hier.
  window.addEventListener('ui:notify', e => {
    const d = (e && e.detail) || {};
    push({
      time: d.time || new Date().toLocaleTimeString(),
      level: d.level || 'INFO',
      message: d.message || '',
    });
  });

  // Also mirror generator progress into the log so Go/Python path lines are copyable.
  EventsOn('generate:progress', line => {
    push({ time: new Date().toLocaleTimeString(), level: 'INFO', message: String(line) });
  });
  EventsOn('generate:done', result => {
    if (result && result.error) {
      push({ time: new Date().toLocaleTimeString(), level: 'ERROR', message: 'Generation: ' + result.error });
      return;
    }
    if (result && result.path) {
      const pipe = result.pipeline === 'go'
        ? `Go (${result.tracking || 'native'}/${result.backend || '?'})`
        : 'Python';
      push({ time: new Date().toLocaleTimeString(), level: 'INFO',
        message: `Generation finished [${pipe}]: ${result.path}` });
    }
  });

  el('#log-errors-only').addEventListener('change', render);
  el('#log-clear').addEventListener('click', () => {
    entries = [];
    ClearLogEntries().catch(() => {});
    render();
    el('#log-status').textContent = 'Cleared.';
  });
  el('#log-open-folder').addEventListener('click', () => {
    OpenLogFolder().catch(err => { el('#log-status').textContent = String(err); });
  });
  el('#log-copy').addEventListener('click', async () => {
    const text = view.textContent || '';
    try {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
      } else {
        const range = document.createRange();
        range.selectNodeContents(view);
        const sel = window.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
        document.execCommand('copy');
      }
      el('#log-status').textContent = 'Copied.';
    } catch (err) {
      // Fallback: select all so user can Cmd/Ctrl+C
      const range = document.createRange();
      range.selectNodeContents(view);
      const sel = window.getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
      el('#log-status').textContent = 'Selected — press Cmd/Ctrl+C.';
    }
  });
}
