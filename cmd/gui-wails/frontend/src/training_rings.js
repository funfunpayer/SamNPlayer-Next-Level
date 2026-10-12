/**
 * C2: Device-adaptive Flow training rings.
 *
 * createRings(host, ['vibration', 'suction']) -> {
 *   update({ vibration: 0.42, suction: 0.71 }), setPhase('Plateau'), destroy()
 * }
 *
 * Values MUST come from the actual training/device output, normalized to 0..1.
 * A missing value stays "—" (unknown), never a simulated zero. The miniature
 * bars are an intensity indicator, NOT an audio waveform or sensor history.
 * No Wails imports, network calls, timers or hard-coded device capabilities.
 * Claude owns mounting, engine events, caps and emergency stop.
 *
 * Host should import './training_rings.css' separately.
 */

const DEFAULT_LABELS = Object.freeze({
  vibration: 'Vibration',
  suction: 'Suction',
  stroke: 'Stroke',
  twist: 'Twist',
  roll: 'Roll',
  pitch: 'Pitch',
  surge: 'Surge',
  sway: 'Sway',
  rotate: 'Rotate',
});
const PAUSE_PHASES = new Set(['paused', 'pause', 'idle', 'stopped', 'ready']);
const MAX_CHANNELS = 16;

function channelSpecs(channels) {
  if (!Array.isArray(channels)) throw new TypeError('channels must be an array');
  if (channels.length > MAX_CHANNELS) throw new RangeError('Too many channels');
  const seen = new Set();
  return channels.map((entry) => {
    const key = typeof entry === 'string' ? entry : entry?.id ?? entry?.axis ?? entry?.key;
    if (typeof key !== 'string' || !/^[a-z][a-z0-9_-]{0,39}$/i.test(key)) {
      throw new TypeError('Each channel needs a safe id (letters, digits, - or _)');
    }
    const id = key.toLowerCase();
    if (seen.has(id)) throw new RangeError('Duplicate channel: ' + id);
    seen.add(id);
    const name = typeof entry === 'object' && typeof entry.label === 'string' && entry.label.trim()
      ? entry.label.trim().slice(0, 64)
      : DEFAULT_LABELS[id] || id.replace(/[-_]/g, ' ').replace(/^./, (s) => s.toUpperCase());
    return { id, name };
  });
}

function createChannelCard(spec, index) {
  const card = document.createElement('div');
  card.className = 'flow-tr__channel';
  card.dataset.axis = spec.id;
  card.classList.toggle('flow-tr__channel--featured', index < 2);
  card.style.setProperty('--flow-tr-accent',
    spec.id === 'suction' ? '#ff982e' : spec.id === 'vibration' ? '#03d8f2' : '#8ed7ff');

  const heading = document.createElement('div');
  heading.className = 'flow-tr__heading';
  const name = document.createElement('span');
  name.className = 'flow-tr__name';
  name.textContent = spec.name;
  const value = document.createElement('output');
  value.className = 'flow-tr__value';
  value.textContent = '—';
  value.setAttribute('aria-label', spec.name + ' output: unavailable');
  heading.append(name, value);

  const visual = document.createElement('div');
  visual.className = 'flow-tr__visual';
  visual.innerHTML = `<svg class="flow-tr__ring" viewBox="0 0 160 160" aria-hidden="true" focusable="false">
    <circle class="flow-tr__track" cx="80" cy="80" r="58"/>
    <circle class="flow-tr__halo" cx="80" cy="80" r="58"/>
    <circle class="flow-tr__arc" cx="80" cy="80" r="58"
      stroke-dasharray="364.425" stroke-dashoffset="364.425" transform="rotate(-90 80 80)"/>
    <circle class="flow-tr__inner" cx="80" cy="80" r="43"/>
  </svg>`;
  const center = document.createElement('div');
  center.className = 'flow-tr__center';
  center.textContent = '—';
  center.setAttribute('aria-hidden', 'true');
  visual.append(center);

  const meter = document.createElement('div');
  meter.className = 'flow-tr__meter';
  meter.setAttribute('aria-hidden', 'true');
  for (let i = 0; i < 11; i++) {
    const bar = document.createElement('span');
    bar.className = 'flow-tr__meter-bar';
    meter.append(bar);
  }
  // Sparkline contains only successive real readings; no invented samples.
  const spark = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  spark.classList.add('flow-tr__spark');
  spark.setAttribute('viewBox', '0 0 120 28');
  spark.setAttribute('aria-hidden', 'true');
  const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
  line.classList.add('flow-tr__spark-line');
  line.setAttribute('fill', 'none');
  spark.append(line);
  const note = document.createElement('span');
  note.className = 'flow-tr__meter-note';
  note.textContent = 'Output level';
  card.append(heading, visual, meter, spark, note);
  return {
    card,
    value,
    center,
    arc: visual.querySelector('.flow-tr__arc'),
    bars: [...meter.children],
    line,
    history: [],
  };
}

function normalValue(raw) {
  // Do not treat null, undefined, NaN, Infinity or strings as real readings.
  return typeof raw === 'number' && Number.isFinite(raw) ? Math.max(0, Math.min(1, raw)) : null;
}

/**
 * @param {HTMLElement} el Mount point; must exist in the document.
 * @param {Array<string|{id?:string,axis?:string,key?:string,label?:string}>} channels
 * @returns {{update:(values:Record<string,number|null>)=>void,setPhase:(name:string)=>void,destroy:()=>void}}
 */
export function createRings(el, channels) {
  if (!(el instanceof HTMLElement)) throw new TypeError('createRings expects an HTMLElement');
  const specs = channelSpecs(channels);
  const root = document.createElement('section');
  root.className = 'flow-tr';
  root.setAttribute('aria-label', 'Live training output');
  const header = document.createElement('div');
  header.className = 'flow-tr__header';
  const title = document.createElement('strong');
  title.textContent = 'LIVE TRAINING';
  const phase = document.createElement('span');
  phase.className = 'flow-tr__phase';
  phase.textContent = '—';
  phase.setAttribute('aria-label', 'Current training phase unavailable');
  header.append(title, phase);
  const grid = document.createElement('div');
  grid.className = 'flow-tr__grid';
  const views = new Map();
  for (const [index, spec] of specs.entries()) {
    const view = createChannelCard(spec, index);
    views.set(spec.id, view);
    grid.append(view.card);
  }
  if (!specs.length) {
    const empty = document.createElement('p');
    empty.className = 'flow-tr__empty';
    empty.textContent = 'No training output channels available for this device.';
    grid.append(empty);
  }
  root.append(header, grid);
  el.append(root);

  let destroyed = false;
  let inViewport = true;
  const mq = typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-reduced-motion: reduce)') : null;
  const refresh = () => {
    if (destroyed) return;
    root.classList.toggle('flow-tr--animate',
      !document.hidden && root.isConnected && inViewport && !mq?.matches
      && !root.classList.contains('flow-tr--paused'));
  };
  const onVisibility = () => refresh();
  const observer = typeof IntersectionObserver === 'function'
    ? new IntersectionObserver((entries) => {
      inViewport = entries.some((entry) => entry.isIntersecting);
      refresh();
    }) : null;
  document.addEventListener('visibilitychange', onVisibility);
  mq?.addEventListener?.('change', onVisibility);
  observer?.observe(root);
  refresh();

  return {
    update(values) {
      if (destroyed) return;
      if (!values || typeof values !== 'object' || Array.isArray(values)) {
        throw new TypeError('update expects a map of real normalized output values');
      }
      for (const [id, view] of views) {
        if (!Object.prototype.hasOwnProperty.call(values, id)) continue;
        const value = normalValue(values[id]);
        const text = value === null ? '—' : Math.round(value * 100) + '%';
        view.value.textContent = text;
        view.center.textContent = text;
        view.value.setAttribute('aria-label',
          specs.find((spec) => spec.id === id).name + ' output: '
          + (value === null ? 'unavailable' : text));
        // Unknown readings interrupt the trace; never bridge gaps with fake values.
        if (value === null) view.history.length = 0;
        else {
          view.history.push(value);
          if (view.history.length > 24) view.history.shift();
        }
        view.line.setAttribute('points', view.history.length < 2 ? '' :
          view.history.map((v, i) => (i * 120 / 23).toFixed(1)
            + ',' + (26 - v * 24).toFixed(1)).join(' '));
        view.card.dataset.available = String(value !== null);
        view.card.dataset.active = String(value !== null && value > 0);
        view.card.style.setProperty('--flow-tr-level', value === null ? '0' : String(value));
        view.arc.style.strokeDashoffset = String(364.425 * (1 - (value ?? 0)));
        view.bars.forEach((bar, index) => {
          // Deterministic level indicator, not a fictional live waveform.
          const fraction = (index + 1) / view.bars.length;
          bar.style.height = value === null ? '3px' : Math.round(3 + 23 * Math.min(1, value / fraction)) + 'px';
          bar.style.opacity = value === null ? '.18' : String(.18 + .75 * Math.min(1, value / fraction));
        });
      }
    },
    setPhase(name) {
      if (destroyed) return;
      const label = typeof name === 'string' ? name.trim().slice(0, 100) : '';
      phase.textContent = label || '—';
      phase.setAttribute('aria-label', label ? 'Current training phase: ' + label : 'Current training phase unavailable');
      root.classList.toggle('flow-tr--paused', !label || PAUSE_PHASES.has(label.toLowerCase()));
      refresh();
    },
    destroy() {
      if (destroyed) return;
      destroyed = true;
      observer?.disconnect();
      document.removeEventListener('visibilitychange', onVisibility);
      mq?.removeEventListener?.('change', onVisibility);
      root.remove();
      views.clear();
    },
  };
}
