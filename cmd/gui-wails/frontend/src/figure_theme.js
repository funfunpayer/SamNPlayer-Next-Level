/** Claude figure system — shared visual DNA across Device / AI Train / Training.
 *
 * Claude shipped the Device-tab shell fill (teal suction + amber vibration).
 * AI Train body map and Training pixel pair reuse the same heat language so
 * the three figure surfaces feel like one system, not three doodles.
 *
 * Ownership (AGENT_COORD): Claude owns silhouette taste; Cursor wires product
 * surfaces. Prefer changing tokens here before forking colors in callers.
 */

/** Device shell / Contact / body-map zone teal (Claude). */
export const FIGURE_TEAL = '#3dccc0';
/** Device vib / chrome accent — lilac (Owner Look v3, 26 Sep). */
export const FIGURE_AMBER = '#b896e8';
/** Peak / warn coral — beyond accent when intensity is high. */
export const FIGURE_CORAL = '#ef5f5f';
/** Dim body pixels / outline cool. */
export const FIGURE_COOL = '#3a4558';
export const FIGURE_COOL_PARTNER = '#454e62';
export const FIGURE_OUTLINE = '#1a2030';
export const FIGURE_GROUND = '#1e2533';

/** Deep teal used at low fill (matches Device shell base). */
export const FIGURE_TEAL_DEEP = '#2a6b66';

/**
 * Heat ramp for intensity 0..1 — same stops as Device vib/suc fills:
 * cool teal → Claude teal → lilac → coral. Returns #rrggbb.
 */
export function figureHeatColor(level01) {
  const t = Math.max(0, Math.min(1, level01));
  if (t < 0.45) {
    return lerpHex(FIGURE_TEAL_DEEP, FIGURE_TEAL, t / 0.45);
  }
  if (t < 0.7) {
    return lerpHex(FIGURE_TEAL, FIGURE_AMBER, (t - 0.45) / 0.25);
  }
  return lerpHex(FIGURE_AMBER, FIGURE_CORAL, (t - 0.7) / 0.3);
}

function lerpHex(a, b, t) {
  const pa = hexToRgb(a), pb = hexToRgb(b);
  const r = Math.round(pa.r + (pb.r - pa.r) * t);
  const g = Math.round(pa.g + (pb.g - pa.g) * t);
  const bl = Math.round(pa.b + (pb.b - pa.b) * t);
  return `#${[r, g, bl].map(n => n.toString(16).padStart(2, '0')).join('')}`;
}

function hexToRgb(hex) {
  const h = hex.replace('#', '');
  return {
    r: parseInt(h.slice(0, 2), 16),
    g: parseInt(h.slice(2, 4), 16),
    b: parseInt(h.slice(4, 6), 16),
  };
}

/** CSS snippet for AI Train vector body map (Claude zones). */
export function bodyMapZoneCSS() {
  return `
      .bf-outline { fill: none; stroke: #5a6478; stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
      .bf-zone { fill: rgba(61,204,192,0.08); stroke: ${FIGURE_TEAL}; stroke-width: 1.2; cursor: pointer; transition: fill .12s ease; }
      .bf-zone:hover, .bf-zone.is-hot { fill: rgba(61,204,192,0.28); }
      .bf-zone.is-active { fill: rgba(184,150,232,0.35); stroke: ${FIGURE_AMBER}; stroke-width: 1.8; }
      .bf-label { fill: #9aa3b5; font-size: 7px; font-family: inherit; pointer-events: none; }
  `;
}
