/** Canonical English body-region taxonomy (mirrors generator/bodyparts). */

/** Free tag boxes per training frame (assign labels afterward; duplicates OK). */
export const MAX_REGIONS = 12;

export const CANONICAL = [
  { id: 'face', label: 'Face', aliases: ['kopf', 'gesicht'] },
  { id: 'mouth', label: 'Mouth', aliases: ['mund', 'lips'] },
  { id: 'breasts', label: 'Breasts', aliases: ['brust', 'breast', 'tits'] },
  { id: 'nipples', label: 'Nipples', aliases: ['nipple', 'nippel', 'brustwarze', 'brustwarzen'] },
  { id: 'hand_1', label: 'Hand 1', aliases: ['hand', 'hand1', 'left_hand'] },
  { id: 'hand_2', label: 'Hand 2', aliases: ['hand2', 'right_hand'] },
  { id: 'penis', label: 'Penis', aliases: [] },
  { id: 'glans', label: 'Glans', aliases: ['eichel', 'tip'] },
  { id: 'vagina', label: 'Vagina', aliases: ['pussy', 'vulva'] },
];

export const CLASS_PRESETS = CANONICAL.map(p => p.id);

/**
 * Contact-vibe Create UI — Owner minimal set (not full taxonomy).
 * Tip (tracked CSRT): glans / penis.
 * Contact partners (touch feel): mouth, nipple/breast.
 * Full CANONICAL stays for AI Train / Scene-map Region / Tf/Tj.
 */
export const CONTACT_VIBE_TIP_IDS = ['glans', 'penis'];
export const CONTACT_VIBE_PARTNER_IDS = ['mouth', 'nipples', 'breasts'];
export const CONTACT_VIBE_IDS = [
  ...CONTACT_VIBE_TIP_IDS,
  ...CONTACT_VIBE_PARTNER_IDS,
];

/** Zone 2 / contact proposals — contact-vibe minimal (mouth + nipple/breast). */
export const CONTACT_CLASS_ORDER = [...CONTACT_VIBE_PARTNER_IDS];

/** Zone 1 tip proposals — glans/penis for Everyday CSRT + contact vibe. */
export const TIP_CLASS_ORDER = [...CONTACT_VIBE_TIP_IDS];

/**
 * Full BODY_REGIONS order for strict AI expected-class (Advanced / Smarter tip find).
 * Prefer tip-like ids first, then partners (nipples/breasts/…) so trained free-tag
 * classes are selectable — not glans-only.
 */
export const AI_TIP_CLASS_ORDER = [
  'glans', 'penis', 'hand_1', 'hand_2',
  'nipples', 'breasts', 'mouth', 'vagina', 'face',
];

/**
 * Full contact-ish order for Scene-map Region labels (training / review).
 * Not the Everyday Contact-vibe dropdown.
 */
export const SCENE_REGION_CLASS_ORDER = [
  'mouth', 'nipples', 'breasts', 'hand_1', 'hand_2', 'vagina', 'face',
  'glans', 'penis',
];

const ALIAS = (() => {
  const m = Object.create(null);
  for (const p of CANONICAL) {
    m[p.id] = p.id;
    for (const a of p.aliases || []) m[String(a).toLowerCase()] = p.id;
  }
  return m;
})();

/** Map free-text / German legacy labels to canonical English IDs. */
export function normalizeClass(name) {
  const s = String(name || '').trim().toLowerCase().replace(/[\s-]+/g, '_');
  if (!s) return '';
  return ALIAS[s] || s;
}

export function labelFor(id) {
  const n = normalizeClass(id);
  const hit = CANONICAL.find(p => p.id === n);
  return hit ? hit.label : (id || '');
}

/** Typical generate role for a class (docs/BODY_REGIONS.md) — label metadata only. */
export function defaultRole(id) {
  switch (normalizeClass(id)) {
    case 'penis':
    case 'glans':
    case 'hand_1':
    case 'hand_2':
      return 'tracked';
    case 'nipples':
    case 'mouth':
    case 'vagina':
    case 'breasts':
      return 'fixed';
    case 'face':
      return 'mask';
    default:
      return 'tracked';
  }
}

/** UI option text: "Face (mask)", "Glans (tracked)", … */
export function labelWithRole(id) {
  const lab = labelFor(id) || id;
  const role = defaultRole(id);
  return role ? `${lab} (${role})` : lab;
}

/** Ordered CANONICAL entries for a select; leftovers appended in taxonomy order. */
export function orderedCanonical(preferIds) {
  const seen = new Set();
  const out = [];
  for (const id of preferIds || []) {
    const n = normalizeClass(id);
    const hit = CANONICAL.find(p => p.id === n);
    if (hit && !seen.has(hit.id)) {
      seen.add(hit.id);
      out.push(hit);
    }
  }
  for (const p of CANONICAL) {
    if (!seen.has(p.id)) out.push(p);
  }
  return out;
}

/** Only the listed canonical IDs (no leftover taxonomy) — Contact-vibe Create. */
export function onlyCanonical(ids) {
  const seen = new Set();
  const out = [];
  for (const id of ids || []) {
    const n = normalizeClass(id);
    const hit = CANONICAL.find(p => p.id === n);
    if (hit && !seen.has(hit.id)) {
      seen.add(hit.id);
      out.push(hit);
    }
  }
  return out;
}
