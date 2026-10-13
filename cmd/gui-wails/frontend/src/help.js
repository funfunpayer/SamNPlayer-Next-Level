// Shared option help: small "?" chips with a popover explaining what a
// control does. Keeps the form itself short while answering “what is this?”.

export function helpChip(text) {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'help-chip';
  btn.setAttribute('aria-label', 'Help');
  btn.textContent = '?';
  btn.dataset.help = text;
  return btn;
}

export function attachHelp(anchor, text) {
  if (!anchor || !text) return;
  const existing = anchor.querySelector(':scope > .help-chip')
    || (anchor.parentElement && anchor.parentElement.querySelector(':scope > .help-chip[data-for="' + (anchor.id || '') + '"]'));
  if (existing) {
    existing.dataset.help = text;
    return existing;
  }
  const chip = helpChip(text);
  if (anchor.id) chip.dataset.for = anchor.id;
  // Labels: Chip ans Label hängen, damit field-row/checkbox-row-Layout hält.
  if (anchor.tagName === 'LABEL' || anchor.classList.contains('help-anchor')) {
    anchor.appendChild(chip);
  } else if (anchor.parentElement) {
    anchor.insertAdjacentElement('afterend', chip);
  } else {
    anchor.appendChild(chip);
  }
  return chip;
}

let openPopover = null;

function closeHelp() {
  if (openPopover) {
    openPopover.remove();
    openPopover = null;
  }
}

function openHelp(chip) {
  closeHelp();
  const tip = document.createElement('div');
  tip.className = 'help-popover';
  tip.textContent = chip.dataset.help || '';
  document.body.appendChild(tip);
  const r = chip.getBoundingClientRect();
  const pad = 8;
  let left = r.left;
  let top = r.bottom + 6;
  tip.style.left = left + 'px';
  tip.style.top = top + 'px';
  const box = tip.getBoundingClientRect();
  if (box.right > window.innerWidth - pad) {
    tip.style.left = Math.max(pad, window.innerWidth - box.width - pad) + 'px';
  }
  if (box.bottom > window.innerHeight - pad) {
    tip.style.top = Math.max(pad, r.top - box.height - 6) + 'px';
  }
  openPopover = tip;
}

document.addEventListener('click', (e) => {
  const chip = e.target.closest && e.target.closest('.help-chip');
  if (chip) {
    e.preventDefault();
    e.stopPropagation();
    if (openPopover && openPopover._chip === chip) {
      closeHelp();
      return;
    }
    openHelp(chip);
    if (openPopover) openPopover._chip = chip;
    return;
  }
  closeHelp();
});

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') closeHelp();
});

/** Wire [data-help] on any element: inserts a "?" after it. */
export function wireDataHelp(root) {
  if (!root) return;
  root.querySelectorAll('[data-help]').forEach(el => {
    if (el.classList.contains('help-chip')) return;
    attachHelp(el, el.getAttribute('data-help'));
  });
}
