import { ReviewGeneratedScript } from '../wailsjs/go/main/App';
import { EventsOn } from '../wailsjs/runtime/runtime';

// Nach dem Erzeugen: Hinweise in den Status/Log, kein Popup außer bei
// bewusster Invert-Aktion über einen Knopf (falls vorgeschlagen).
export function initPostGenerateReview() {
  EventsOn('generate:done', async (payload) => {
    if (!payload || payload.error || !payload.path) return;
    let review;
    try {
      review = await ReviewGeneratedScript(payload.path);
    } catch (err) {
      return;
    }
    const pol = review.polarity || {};
    const ozone = review.ozone || {};
    const bits = [];
    if (pol.reason) bits.push('Direction: ' + pol.reason);
    if (ozone.ok) bits.push('O-zone suggested: ' + Math.round(ozone.startMs / 1000) + 's–' + Math.round(ozone.endMs / 1000) + 's');
    else if (ozone.reason) bits.push('O-zone: ' + ozone.reason);
    const status = document.querySelector('#gen-status');
    if (status && bits.length) {
      status.textContent = (status.textContent ? status.textContent + ' \n' : '') + bits.join(' ');
    }
    // Invert nur vorschlagen, nicht per Popup erzwingen — Nutzer prüft in der Playback.
    if (pol.suggestInvert && status) {
      status.textContent += ' (Direction uncertain — review in Play; invert curve if needed.)';
    }
  });
}
