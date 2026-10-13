/**
 * SamNPlayer Flow — decorative idle-stage visual for Play and Create.
 *
 * Claude wiring contract:
 *   const fx = mountFlowStage(document.querySelector('#pb-stage-idle'));
 *   fx.setState('playing' | 'paused' | 'idle');
 *   fx.destroy();
 *
 * This module does not import Wails, load media, run timers, or set player state.
 * Import './flow_fx.css' from the host entrypoint or the mounting module.
 */
const STATES = new Set(['idle', 'playing', 'paused']);
let nextInstanceId = 0;
const SVG = `
<svg class="flow-fx__art" viewBox="0 0 800 450" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false">
  <defs>
    <linearGradient id="flow-fx-cyan" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#a2faff"/>
      <stop offset=".52" stop-color="#07daf8"/>
      <stop offset="1" stop-color="#007ca9"/>
    </linearGradient>
    <linearGradient id="flow-fx-amber" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#ffcb7d"/>
      <stop offset=".55" stop-color="#ff922c"/>
      <stop offset="1" stop-color="#bd3f0b"/>
    </linearGradient>
    <radialGradient id="flow-fx-halo">
      <stop offset="0" stop-color="#15c7e9" stop-opacity=".22"/>
      <stop offset=".5" stop-color="#057493" stop-opacity=".08"/>
      <stop offset="1" stop-color="#03101e" stop-opacity="0"/>
    </radialGradient>
    <filter id="flow-fx-glow" x="-40%" y="-80%" width="180%" height="260%">
      <feGaussianBlur stdDeviation="5"/>
    </filter>
  </defs>
  <ellipse class="flow-fx__halo" cx="400" cy="221" rx="295" ry="180" fill="url(#flow-fx-halo)"/>
  <g class="flow-fx__ribbons">
    <path class="flow-fx__trail flow-fx__trail--back" d="M105 295 C230 345 295 303 354 251 S510 142 695 174"
      fill="none" stroke="url(#flow-fx-cyan)" stroke-width="12" stroke-linecap="round" opacity=".22" filter="url(#flow-fx-glow)"/>
    <path class="flow-fx__trail flow-fx__trail--cyan" d="M105 295 C230 345 295 303 354 251 S510 142 695 174"
      fill="none" stroke="url(#flow-fx-cyan)" stroke-width="3" stroke-linecap="round" opacity=".82"/>
    <path class="flow-fx__trail flow-fx__trail--amber" d="M137 150 C248 110 305 177 385 232 S540 320 663 265"
      fill="none" stroke="url(#flow-fx-amber)" stroke-width="4.2" stroke-linecap="round" opacity=".84"/>
    <path class="flow-fx__trail flow-fx__trail--detail" d="M183 309 C271 320 320 280 372 239 S519 164 609 180"
      fill="none" stroke="#c9fbff" stroke-width="1.2" stroke-linecap="round" opacity=".55"/>
  </g>
  <g class="flow-fx__mark" transform="translate(400 222)">
    <path d="M-56 -75 Q-63 -81 -66 -72 L-66 76 Q-65 86 -54 80 L85 8 Q97 1 85 -8 Z"
      fill="#04a6d4" opacity=".23" filter="url(#flow-fx-glow)"/>
    <path d="M-52 -71 Q-59 -76 -59 -66 L-59 66 Q-58 77 -47 72 L73 8 Q86 0 73 -8 Z"
      fill="url(#flow-fx-cyan)" stroke="#b4f9ff" stroke-width="2.4" stroke-linejoin="round"/>
    <path d="M-36 -43 L-36 43 L46 0 Z" fill="#051d32" opacity=".67"/>
    <path d="M-49 -57 L62 0" stroke="#d0fdff" opacity=".55" stroke-width="2" stroke-linecap="round"/>
    <path d="M-47 61 L63 1" stroke="#006c94" opacity=".6" stroke-width="3" stroke-linecap="round"/>
  </g>
</svg>`;

function visibilityEnabled(root, inViewport, reducedMotion) {
  return !reducedMotion && inViewport && !document.hidden && root.isConnected;
}

/**
 * Mount decorative graphics into a host element.
 * @param {HTMLElement} el
 * @param {{state?: 'idle'|'playing'|'paused', label?: string}} [opts]
 * @returns {{setState: (state: 'idle'|'playing'|'paused') => void, destroy: () => void}}
 */
export function mountFlowStage(el, opts = {}) {
  if (!(el instanceof HTMLElement)) throw new TypeError('mountFlowStage expects an HTMLElement');
  const root = document.createElement('div');
  root.className = 'flow-fx';
  root.setAttribute('role', 'img');
  root.setAttribute('aria-label', opts.label || 'SamNPlayer Flow');
  // Scope gradient/filter IDs because Play and Create can mount at the same time.
  const suffix = '-' + (++nextInstanceId);
  root.innerHTML = SVG.replaceAll('flow-fx-cyan', 'flow-fx-cyan' + suffix)
    .replaceAll('flow-fx-amber', 'flow-fx-amber' + suffix)
    .replaceAll('flow-fx-halo', 'flow-fx-halo' + suffix)
    .replaceAll('flow-fx-glow', 'flow-fx-glow' + suffix); // Static SVG; no user input.
  el.appendChild(root);

  let destroyed = false;
  let inViewport = true;
  const mq = typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-reduced-motion: reduce)')
    : null;
  const refresh = () => {
    if (destroyed) return;
    root.classList.toggle('flow-fx--animate', visibilityEnabled(root, inViewport, !!mq?.matches));
  };
  const setState = (value) => {
    if (destroyed) return;
    if (!STATES.has(value)) throw new RangeError('Unknown Flow stage state: ' + value);
    root.dataset.state = value;
  };
  const onVisibility = () => refresh();
  const observer = typeof IntersectionObserver === 'function'
    ? new IntersectionObserver((entries) => {
      inViewport = entries.some((entry) => entry.isIntersecting);
      refresh();
    }, { threshold: 0 })
    : null;

  document.addEventListener('visibilitychange', onVisibility);
  mq?.addEventListener?.('change', onVisibility);
  observer?.observe(root);
  setState(STATES.has(opts.state) ? opts.state : 'idle');
  refresh();

  return {
    setState,
    destroy() {
      if (destroyed) return;
      destroyed = true;
      observer?.disconnect();
      document.removeEventListener('visibilitychange', onVisibility);
      mq?.removeEventListener?.('change', onVisibility);
      root.remove();
    },
  };
}
