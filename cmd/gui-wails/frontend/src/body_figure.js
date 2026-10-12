/** Human body map for AI Train class picking (stylized line figure).
 *
 * Claude figure system (`figure_theme.js`): same teal/amber as Device shell.
 * Click a region → select that body-part class. Anatomical outline only.
 * Training-tab motion uses pixel pairs (`pixel_figure.js`) on the same theme.
 */

import { CANONICAL, labelFor, normalizeClass } from './bodyparts.js';
import { bodyMapZoneCSS } from './figure_theme.js';

const REGION_HINTS = {
  face: 'Head / face box',
  mouth: 'Mouth / lips (contact vibe)',
  breasts: 'Breast contact (Owner: nipple/breast)',
  nipples: 'Nipple contact (Owner: nipple/breast)',
  hand_1: 'Hand 1',
  hand_2: 'Hand 2',
  penis: 'Penis / shaft (tip)',
  glans: 'Glans / tip (tracked tip)',
  vagina: 'Vagina / vulva',
};

/** Inline SVG — viewBox 0 0 120 200, front-facing stylized adult. */
export function bodyFigureMarkup() {
  return `
<svg class="body-figure-svg" viewBox="0 0 120 200" role="img"
     aria-label="Body parts for AI training">
  <defs>
    <style>${bodyMapZoneCSS()}</style>
  </defs>
  <!-- soft body silhouette (non-interactive) -->
  <ellipse class="bf-outline" cx="60" cy="28" rx="16" ry="18"/>
  <path class="bf-outline" d="M44 44 Q60 52 76 44 L82 90 Q60 102 38 90 Z"/>
  <path class="bf-outline" d="M42 92 L38 150 M78 92 L82 150"/>
  <path class="bf-outline" d="M38 150 L34 196 M82 150 L86 196"/>
  <path class="bf-outline" d="M44 58 L22 88 M76 58 L98 88"/>

  <!-- clickable zones -->
  <ellipse class="bf-zone" data-class="face" cx="60" cy="26" rx="14" ry="15"/>
  <ellipse class="bf-zone" data-class="mouth" cx="60" cy="36" rx="7" ry="3.5"/>
  <ellipse class="bf-zone" data-class="breasts" cx="60" cy="68" rx="22" ry="14"/>
  <circle class="bf-zone" data-class="nipples" cx="50" cy="68" r="3.2"/>
  <circle class="bf-zone" data-class="nipples" cx="70" cy="68" r="3.2" data-alias="nipples-r"/>
  <ellipse class="bf-zone" data-class="hand_1" cx="20" cy="92" rx="9" ry="7"/>
  <ellipse class="bf-zone" data-class="hand_2" cx="100" cy="92" rx="9" ry="7"/>
  <ellipse class="bf-zone" data-class="penis" cx="60" cy="118" rx="7" ry="16"/>
  <ellipse class="bf-zone" data-class="glans" cx="60" cy="132" rx="5.5" ry="5"/>
  <ellipse class="bf-zone" data-class="vagina" cx="60" cy="108" rx="10" ry="6"/>
</svg>`;
}

/**
 * Mount interactive body map into `host`.
 * @param {HTMLElement} host
 * @param {{
 *   onSelect?: (classId: string) => void,
 *   getActive?: () => string,
 *   allowedClasses?: string[],  // Contact-vibe Create: minimal set only
 *   heading?: string,
 *   hint?: string,
 * }} opts
 */
export function mountBodyFigure(host, opts = {}) {
  if (!host) return { setActive() {}, destroy() {} };
  const allowed = Array.isArray(opts.allowedClasses) && opts.allowedClasses.length
    ? new Set(opts.allowedClasses.map((id) => normalizeClass(id)).filter(Boolean))
    : null;
  const legendParts = allowed
    ? CANONICAL.filter((c) => allowed.has(c.id))
    : CANONICAL;
  const headStrong = opts.heading || 'Body map';
  const headHint = opts.hint || (allowed
    ? 'Contact vibe: mouth · nipple/breast · tip (glans/penis). Not a stroke path.'
    : 'Free tags: draw boxes, then click a class — same label OK (nipples need two boxes).');
  host.classList.add('body-figure');
  host.innerHTML = `
    <div class="body-figure-card">
      <div class="body-figure-head">
        <strong>${headStrong}</strong>
        <span class="hint">${headHint}</span>
      </div>
      <div class="body-figure-row">
        ${bodyFigureMarkup()}
        <ul class="body-figure-legend"></ul>
      </div>
    </div>`;

  const svg = host.querySelector('.body-figure-svg');
  const legend = host.querySelector('.body-figure-legend');
  let active = normalizeClass(opts.getActive?.() || '') || '';

  function paint() {
    svg.querySelectorAll('.bf-zone').forEach(el => {
      const id = el.getAttribute('data-class');
      const ok = !allowed || allowed.has(id);
      el.classList.toggle('is-active', ok && id === active);
      el.classList.toggle('is-disabled', !ok);
      el.setAttribute('aria-pressed', ok && id === active ? 'true' : 'false');
      el.setAttribute('aria-disabled', ok ? 'false' : 'true');
      el.setAttribute('tabindex', ok ? '0' : '-1');
    });
    legend.querySelectorAll('[data-class]').forEach(li => {
      li.classList.toggle('is-active', li.getAttribute('data-class') === active);
    });
  }

  function select(id) {
    const n = normalizeClass(id);
    if (!n) return;
    if (allowed && !allowed.has(n)) return;
    active = n;
    paint();
    opts.onSelect?.(n);
  }

  legend.innerHTML = legendParts.map(c =>
    `<li data-class="${c.id}" title="${REGION_HINTS[c.id] || c.label}">
       <button type="button" class="body-figure-leg-btn">${c.label}</button>
     </li>`).join('');

  svg.querySelectorAll('.bf-zone').forEach(el => {
    const id = el.getAttribute('data-class');
    const ok = !allowed || allowed.has(id);
    el.setAttribute('role', 'button');
    el.setAttribute('tabindex', ok ? '0' : '-1');
    el.setAttribute('aria-label', labelFor(id) || id);
    el.title = !ok
      ? 'Not used for Contact vibe marks (AI Train / Scene map Region keep full taxonomy)'
      : (!allowed && id === 'nipples')
        ? 'Nipples — each side is its own free box, same label'
        : (REGION_HINTS[id] || labelFor(id) || id);
    if (!ok) {
      el.style.pointerEvents = 'none';
      el.style.opacity = '0.22';
    }
    el.addEventListener('click', () => select(id));
    el.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); select(id); }
    });
  });
  legend.querySelectorAll('[data-class]').forEach(li => {
    li.querySelector('button')?.addEventListener('click', () => select(li.getAttribute('data-class')));
  });

  paint();
  return {
    setActive(id) {
      active = normalizeClass(id) || '';
      paint();
    },
    destroy() { host.innerHTML = ''; },
  };
}
