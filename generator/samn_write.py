"""Write companion .samn beside a generated .funscript (native source of truth).

Full Neo-2 axis bake lives in Go (samn.BakeNeoAxes). Python writes the
document with general + recipe; Tf/Tj sets playbackSource recipe until the
user/GUI bakes axes or the native Go pipeline writes a baked file.
"""

from __future__ import annotations

import json
import os

from tf_tj_meta import device_recipe_for, is_distance_profile


def companion_samn_path(funscript_path: str) -> str:
    root, _ = os.path.splitext(funscript_path)
    return root + ".samn"


def write_companion_samn(funscript_path, actions, metadata, profile,
                         contact_vibration=False,
                         contact_vibration_span=None,
                         contact_vibration_curve=None):
    """Write clip.samn next to funscript_path. Returns path or None."""
    if not funscript_path or not actions:
        return None
    duration = int(metadata.get("duration") or actions[-1]["at"])
    doc = {
        "version": 1,
        "kind": "samnplayer.script",
        "creator": metadata.get("creator") or "SamNPlayer",
        "durationMs": duration,
        "profile": "tj" if is_distance_profile(profile) else (profile or "standard"),
        "playbackSource": "recipe",
        "general": [{"at": int(a["at"]), "pos": int(a["pos"])} for a in actions],
    }
    recipe = device_recipe_for(
        profile, contact_vibration,
        contact_vibration_span=contact_vibration_span,
        contact_vibration_curve=contact_vibration_curve)
    if recipe:
        doc["recipe"] = recipe
        doc["strengthPresets"] = [
            {"name": "soft", "vibrationScale": 0.7, "suctionScale": 0.85},
            {"name": "normal", "vibrationScale": 1.0, "suctionScale": 1.0},
            {"name": "strong", "vibrationScale": 1.3, "suctionScale": 1.1},
        ]
        doc["activeStrength"] = "normal"
    gaps = metadata.get("tracking_gaps") or []
    if gaps:
        doc["trackingGaps"] = gaps
    if metadata.get("contact_marks"):
        doc["contactMarks"] = metadata["contact_marks"]
    if metadata.get("trajectory"):
        doc["trajectory"] = metadata["trajectory"]
    if "quality_score" in metadata:
        doc["qualityScore"] = metadata["quality_score"]
    if "quality_passed" in metadata:
        doc["qualityPassed"] = metadata["quality_passed"]
    if metadata.get("quality_warnings"):
        doc["qualityWarnings"] = list(metadata["quality_warnings"])

    out = companion_samn_path(funscript_path)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2)
    return out
