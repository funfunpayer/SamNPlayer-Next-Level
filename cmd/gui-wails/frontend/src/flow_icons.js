/**
 * SamNPlayer Flow icon system — C3 (Claude / ChatGPT contract).
 * Consistent 24px, 1.6px strokes, currentColor. No dependencies or Wails.
 *
 * ICONS[name] is an SVG string safe for a trusted template.
 * icon(name, {size, title}) can be used for dynamic DOM creation:
 *   button.append(icon('play', {title: 'Play'}));
 *
 * Icon graphics are decorative by default; buttons MUST keep their own
 * accessible name (visible text or aria-label). No HTML from callers is
 * interpolated into SVG.
 */

const paths = Object.freeze({
  // Global shell / workspace
  player: '<path d="m9 6 10 6-10 6V6Z"/><rect x="2.5" y="3" width="19" height="18" rx="3"/>',
  play: '<path d="m8 5 12 7-12 7V5Z"/>',
  pause: '<path d="M8 5v14M16 5v14"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>',
  resume: '<path d="m8 5 12 7-12 7V5Z"/>',
  create: '<path d="M12 3v18M3 12h18"/><path d="m4 4 3 3M17 17l3 3" opacity=".55"/>',
  edit: '<path d="m15 5 4 4M4 20l4.5-.8L20 7.7 16.3 4 4.8 15.5 4 20Z"/>',
  training: '<path d="M4 14c3 0 3-7 6-7s3 10 6 10 3-7 4-7"/><path d="M3 21h18"/>',
  tools: '<path d="m12 3 2 2.5 3.2-.3.6 3.1 3 1.4-1.2 3 1.2 3-3 1.4-.6 3.1-3.2-.3L12 22l-2-2.5-3.2.3-.6-3.1-3-1.4 1.2-3-1.2-3 3-1.4.6-3.1 3.2.3L12 3Z"/><circle cx="12" cy="12.5" r="3"/>',
  settings: '<path d="M4 7h16M4 17h16M8 4v6M16 14v6"/>',
  device: '<rect x="6" y="3" width="12" height="18" rx="3"/><path d="M10 7h4M10 17h4"/><circle cx="12" cy="12" r="2"/>',
  connection: '<path d="M8 9a5 5 0 0 1 8 0M5 6a9 9 0 0 1 14 0M12 13v7"/><circle cx="12" cy="12" r="1"/>',
  status: '<circle cx="12" cy="12" r="9"/><path d="m8 12 3 3 5-6"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>',
  warning: '<path d="M12 3 2 20h20L12 3Z"/><path d="M12 9v5M12 17h.01"/>',
  log: '<rect x="5" y="3" width="14" height="18" rx="2"/><path d="M8 8h8M8 12h8M8 16h5"/>',

  // Library / media
  library: '<path d="M4 4v16M10 4v16M16 4l4 16M2 20h20"/>',
  folder: '<path d="M3 7V5a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/>',
  file: '<path d="M6 2h8l5 5v15H6V2Z"/><path d="M14 2v6h5"/>',
  video: '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m10 9 5 3-5 3V9Z"/>',
  audio: '<path d="M9 7 5 11H2v4h3l4 4V7ZM13 9c2 1 2 5 0 6M16 6c5 3 5 9 0 12"/>',
  waveform: '<path d="M2 12h2l2-6 3 12 3-15 3 17 3-9 2 1h2"/>',
  chart: '<path d="M3 3v18h18M6 16l4-5 4 3 6-8"/>',
  timeline: '<path d="M3 7h18M3 17h18M7 4v16M16 4v16"/><circle cx="12" cy="12" r="2"/>',
  scenemap: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 10h18M9 4v16M15 10v10"/>',
  layers: '<path d="m12 3 9 5-9 5-9-5 9-5ZM3 12l9 5 9-5M3 16l9 5 9-5"/>',
  bookmark: '<path d="M6 3h12v18l-6-4-6 4V3Z"/>',
  favorite: '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2L12 17.3l-5.6 2.9 1.1-6.2L3 9.6l6.2-.9L12 3Z"/>',
  recent: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 3"/>',
  playlist: '<path d="M3 6h12M3 11h12M3 16h8m6-4 5 3-5 3v-6Z"/>',
  search: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="m15.5 15.5 5 5"/>',
  filter: '<path d="M3 5h18l-7 8v6l-4 2v-8L3 5Z"/>',
  sort: '<path d="M8 3v18m-4-4 4 4 4-4M16 21V3m-4 4 4-4 4 4"/>',
  download: '<path d="M12 3v13m-5-5 5 5 5-5M4 17v4h16v-4"/>',
  upload: '<path d="M12 21V8m-5 5 5-5 5 5M4 7V3h16v4"/>',
  snapshot: '<path d="M3 7h4l2-3h6l2 3h4v13H3V7Z"/><circle cx="12" cy="13" r="4"/>',

  // Transport / actions
  skipBack: '<path d="M5 5v14M19 5 8 12l11 7V5Z"/>',
  skipForward: '<path d="M19 5v14M5 5l11 7-11 7V5Z"/>',
  back10: '<path d="M4 9V4m0 5 5-5M4 9a9 9 0 1 1-1 8"/><path d="M10 11v5M14 11v5"/>',
  forward10: '<path d="M20 9V4m0 5-5-5m5 5a9 9 0 1 0 1 8"/><path d="M10 11v5M14 11v5"/>',
  repeat: '<path d="M4 8V5h15v6M20 16v3H5v-6m-4-5 3 3 3-3m10 8 3-3 3 3"/>',
  shuffle: '<path d="M3 6h3c5 0 7 12 12 12h3m-4-4 4 4-4 4M3 18h3c2 0 3-2 4-4m4-4c1-2 2-4 4-4h3m-4-4 4 4-4 4"/>',
  volume: '<path d="M9 7 5 11H2v4h3l4 4V7ZM13 9c2 1 2 5 0 6M16 6c5 3 5 9 0 12"/>',
  mute: '<path d="M9 7 5 11H2v4h3l4 4V7ZM15 9l6 6M21 9l-6 6"/>',
  fullscreen: '<path d="M9 3H3v6m12-6h6v6M3 15v6h6m12-6v6h-6"/>',
  minimize: '<path d="M9 9H3V3m12 6h6V3M3 21v-6h6m12 6v-6h-6"/>',
  lock: '<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/>',
  unlock: '<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0"/>',
  undo: '<path d="M9 7 4 12l5 5M4 12h11a5 5 0 0 1 0 10"/>',
  redo: '<path d="m15 7 5 5-5 5m5-5H9a5 5 0 0 0 0 10"/>',
  save: '<path d="M4 3h14l3 3v15H4V3Z"/><path d="M8 3v7h9V3M8 21v-8h9v8"/>',
  refresh: '<path d="M20 9V4l-4 4M4 15v5l4-4M20 9a8 8 0 0 0-14-3M4 15a8 8 0 0 0 14 3"/>',
  close: '<path d="M5 5l14 14M19 5 5 19"/>',
  more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
  chevronDown: '<path d="m5 9 7 7 7-7"/>',
  chevronRight: '<path d="m9 5 7 7-7 7"/>',
  chevronLeft: '<path d="m15 5-7 7 7 7"/>',
  check: '<path d="m4 12 5 5L20 6"/>',
  plus: '<path d="M12 4v16M4 12h16"/>',
  minus: '<path d="M4 12h16"/>',
  trash: '<path d="M4 7h16M9 7V4h6v3m3 0-1 14H7L6 7M10 11v6m4-6v6"/>',

  // Inspector / editing / curves
  sliders: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="2"/><circle cx="15" cy="17" r="2"/>',
  effects: '<path d="m12 2 2.5 7.5L22 12l-7.5 2.5L12 22l-2.5-7.5L2 12l7.5-2.5L12 2Z"/>',
  curve: '<path d="M3 19C8 19 7 5 12 5s4 14 9 14"/><circle cx="3" cy="19" r="1.5"/><circle cx="12" cy="5" r="1.5"/><circle cx="21" cy="19" r="1.5"/>',
  invert: '<path d="M3 5h18M3 19h18M5 15l6-6 4 4 4-4M15 9h4v4"/>',
  limits: '<path d="M4 4v16M20 4v16M8 12h8m-5-3-3 3 3 3m2-6 3 3-3 3"/>',
  link: '<path d="M10 14a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-2 2M14 10a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l2-2"/>',
  eye: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/>',
  eyeOff: '<path d="m3 3 18 18M10 5c.6-.1 1.3-.2 2-.2 6 0 10 7.2 10 7.2a18 18 0 0 1-3.5 4.1M6 7C3.5 9 2 12 2 12s4 7 10 7c1.4 0 2.7-.4 3.8-1"/>',
  shield: '<path d="M12 2 4 5v6c0 5 3 9 8 11 5-2 8-6 8-11V5l-8-3Z"/><path d="m8 12 3 3 5-6"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
});

function svg(inner) {
  return '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">' + inner + '</svg>';
}

/** Frozen, vetted SVG markup. Missing keys return undefined (no dummy icons). */
export const ICONS = Object.freeze(Object.fromEntries(
  Object.entries(paths).map(([name, markup]) => [name, svg(markup)])
));

/** Create a decorative SVG node. Caller provides the button's accessible name. */
export function icon(name, { size = 24 } = {}) {
  if (!Object.prototype.hasOwnProperty.call(ICONS, name)) {
    throw new RangeError('Unknown Flow icon: ' + String(name));
  }
  const px = Number(size);
  if (!Number.isFinite(px) || px < 8 || px > 128) {
    throw new RangeError('Flow icon size must be between 8 and 128');
  }
  const wrapper = document.createElement('span');
  wrapper.className = 'flow-icon';
  wrapper.setAttribute('aria-hidden', 'true');
  wrapper.innerHTML = ICONS[name]; // Trusted, fixed SVG dictionary only.
  const element = wrapper.firstElementChild;
  element.setAttribute('width', String(px));
  element.setAttribute('height', String(px));
  return element;
}

/** Human-readable list for dev QA and adoption. */
export const ICON_NAMES = Object.freeze(Object.keys(ICONS));
