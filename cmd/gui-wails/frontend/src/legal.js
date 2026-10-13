// Freeware terms + donations (Owner 10 Oct 2026: no licence keys —
// free with an EULA, financed by donations).
import { GetEULA, AcceptEULA, DeclineEULA, GetThirdPartyNotices, GetDonateLinks, OpenDonateLink, GetAboutInfo, OpenPartnerSite } from '../wailsjs/go/main/App';

function escapeText(s) {
  return String(s ?? '').replace(/[&<>"']/g, c => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Modal with a scrollable text. buttons: [{label, id, primary, onClick}]
function openLegalDialog(title, text, buttons) {
  const overlay = document.createElement('div');
  overlay.className = 'legal-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.innerHTML = `
    <div class="legal-dialog">
      <h2>${escapeText(title)}</h2>
      <pre class="legal-text">${escapeText(text)}</pre>
      <div class="legal-buttons">${buttons.map(b =>
        `<button type="button" id="${b.id}" class="${b.primary ? '' : 'secondary'}">${escapeText(b.label)}</button>`).join('')}
      </div>
    </div>`;
  document.body.appendChild(overlay);
  const close = () => overlay.remove();
  buttons.forEach(b => overlay.querySelector('#' + b.id)?.addEventListener('click', async () => {
    if (b.onClick) await b.onClick();
    if (b.closes !== false) close();
  }));
  return { overlay, close };
}

// First start / new EULA version: the app stays covered until the user
// accepts; Decline quits.
export async function ensureEULAAccepted() {
  let info;
  try { info = await GetEULA(); } catch (_) { return; }
  if (!info || info.accepted) return;
  return new Promise(resolve => {
    openLegalDialog('SamNPlayer — Licence agreement (EULA)', info.text, [
      { id: 'eula-decline', label: 'Decline and quit', onClick: async () => { await DeclineEULA(); }, closes: false },
      { id: 'eula-accept', label: 'I accept', primary: true, onClick: async () => {
        await AcceptEULA(info.version);
        resolve();
      } },
    ]);
  });
}

export async function showEULA() {
  const info = await GetEULA();
  openLegalDialog('Licence agreement (EULA) — version ' + info.version, info.text,
    [{ id: 'legal-close', label: 'Close', primary: true }]);
}

export async function showThirdPartyNotices() {
  const text = await GetThirdPartyNotices();
  openLegalDialog('Third-party notices', text, [{ id: 'legal-close', label: 'Close', primary: true }]);
}

// Renders donation buttons into box. The appeal text stays visible even
// before a donation page is configured.
export async function renderDonateButtons(box) {
  if (!box) return;
  let links = [];
  try { links = (await GetDonateLinks()) || []; } catch (_) { links = []; }
  box.innerHTML = '';
  if (!links.length) {
    box.innerHTML = '<span class="hint" style="margin:0;">Donation page coming soon.</span>';
    return;
  }
  for (const l of links) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'donate-btn';
    btn.textContent = `♥ Donate via ${l.name}`;
    btn.addEventListener('click', () => OpenDonateLink(l.url));
    box.appendChild(btn);
  }
}

// Settings → About SamNPlayer: version, edition, partner site (link appears
// once its URL is set in legal.PartnerSiteURL).
export async function renderAbout(root) {
  const q = sel => root.querySelector(sel);
  let info = {};
  try { info = (await GetAboutInfo()) || {}; } catch (_) { info = {}; }
  const v = q('#st-about-version');
  if (v) v.textContent = `Version ${info.version || '?'} · ${info.edition === 'suite' ? 'Suite edition' : 'Free edition'} · EULA ${info.eulaVersion || ''}`;
  const name = info.partnerName || 'our partner site';
  const txt = q('#st-about-partner-text');
  const btn = q('#st-about-partner');
  if (info.partnerUrl) {
    if (txt) txt.textContent = `In partnership with ${name} — guides and articles around SamNPlayer.`;
    if (btn) {
      btn.disabled = false;
      btn.textContent = `Visit ${name}`;
      btn.onclick = () => OpenPartnerSite();
    }
  } else {
    if (txt) txt.textContent = `In partnership with ${name} — website coming soon.`;
    if (btn) { btn.disabled = true; btn.textContent = 'Website coming soon'; }
  }
}
