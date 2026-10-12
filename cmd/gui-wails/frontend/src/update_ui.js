/** Shared update-check UI helpers — no alert()/confirm(); Patch system can reuse. */

/** Prefer snake_case (GitHub JSON) with PascalCase fallback from some bridges. */
export function releaseTag(release) {
  if (!release || typeof release !== 'object') return '?';
  const tag = release.tag_name || release.TagName || '';
  return String(tag).trim() || '?';
}

/** Short plain preview of release notes (body). */
export function releaseNotesPreview(release, maxLen = 220) {
  if (!release || typeof release !== 'object') return '';
  let body = String(release.body || release.Body || '').trim();
  if (!body) return '';
  const lines = body.split('\n')
    .map(l => l.trim())
    .filter(l => l && !l.startsWith('<!--'))
    .map(l => l.replace(/^[#*_>`\s]+/, ''))
    .filter(Boolean)
    .slice(0, 4);
  let out = lines.join(' · ');
  if (out.length > maxLen) out = out.slice(0, maxLen - 1) + '…';
  return out;
}

/** Stringify Wails/JS errors without empty or junk-only messages. */
export function formatUpdateError(err) {
  if (err == null || err === '') return 'unknown error';
  if (typeof err === 'string') {
    const s = err.trim();
    // Reject protocol-relative leftovers / empty slash-only dialogs.
    if (!s || s === '//' || s === '/' || s === '\\') return 'unknown error';
    return s;
  }
  if (typeof err === 'object') {
    const msg = err.message || err.Message || err.error || err.Error;
    if (msg) return formatUpdateError(String(msg));
  }
  const s = String(err).trim();
  if (!s || s === '//' || s === '[object Object]') return 'unknown error';
  return s;
}
