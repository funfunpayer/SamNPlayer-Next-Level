"""Speed-Cap / Bereich-Löschen müssen Editor-Puffer + Dauer neu laden.

Sonst schreibt Editiermodus die alten rawActions wieder auf die Datei
(Bugbot Sept 2026). Source-level guard.
"""

import pathlib
import re
import sys

from _harness import Checker

FRONTEND = pathlib.Path(__file__).resolve().parents[1]


def main():
    check = Checker()
    src = (FRONTEND / "src" / "playback.js").read_text(encoding="utf-8")

    check("reloadAfterRangeEdit existiert",
          re.search(r"async function reloadAfterRangeEdit\s*\(", src) is not None)

    cap_m = re.search(
        r"el\('#pb-cap-speed'\)[\s\S]*?addEventListener\('click'[\s\S]*?\}\);",
        src,
    )
    check("Cap-Speed-Handler gefunden", cap_m is not None)
    if cap_m:
        block = cap_m.group(0)
        check("Cap-Handler ruft reloadAfterRangeEdit",
              "reloadAfterRangeEdit()" in block)
        check("Cap-Handler nicht nur drawCurve",
              "await drawCurve()" not in block)
        check("Cap-Handler uses adjustable intensity (not hard-coded 400)",
              "capIntensityValue()" in block and "EditCapSpeedRange(axis, marker.startMs, marker.endMs, maxI)" in block)
        check("Cap intensity slider present in markup",
              "id=\"pb-cap-intensity\"" in src)
        check("Cap default 400 in slider",
              re.search(r'id="pb-cap-intensity"[^>]*value="400"', src) is not None)

    del_m = re.search(
        r"el\('#pb-del-range'\)[\s\S]*?addEventListener\('click'[\s\S]*?\}\);",
        src,
    )
    check("Delete-Range-Handler gefunden", del_m is not None)
    if del_m:
        check("Delete-Handler ruft reloadAfterRangeEdit",
              "reloadAfterRangeEdit()" in del_m.group(0))

    fn_m = re.search(
        r"async function reloadAfterRangeEdit\(\)\s*\{([\s\S]*?)\n  \}",
        src,
    )
    check("reloadAfterRangeEdit-Körper gefunden", fn_m is not None)
    if fn_m:
        body = fn_m.group(1)
        check("reload lädt Dauer via LoadFunscript",
              "LoadFunscript(scriptPath)" in body and "durationMs" in body)
        check("reload ruft refreshScriptVisuals",
              "refreshScriptVisuals()" in body)

    return check.report()


if __name__ == "__main__":
    sys.exit(main())
