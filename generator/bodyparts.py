"""Canonical English body-region taxonomy (mirrors generator/bodyparts).

Used by ai_roi / bootstrap so preferred classes and aliases stay in sync
with the Go package and the GUI. See docs/BODY_REGIONS.md.
"""

from __future__ import annotations

CANONICAL = [
    {"id": "face", "label": "Face", "aliases": ["kopf", "gesicht"]},
    {"id": "mouth", "label": "Mouth", "aliases": ["mund", "lips"]},
    {"id": "breasts", "label": "Breasts", "aliases": ["brust", "breast", "tits"]},
    {"id": "nipples", "label": "Nipples",
     "aliases": ["nipple", "nippel", "brustwarze", "brustwarzen"]},
    {"id": "hand_1", "label": "Hand 1", "aliases": ["hand", "hand1", "left_hand", "hand_left"]},
    {"id": "hand_2", "label": "Hand 2", "aliases": ["hand2", "right_hand", "hand_right"]},
    {"id": "penis", "label": "Penis", "aliases": []},
    {"id": "glans", "label": "Glans", "aliases": ["eichel", "tip"]},
    {"id": "vagina", "label": "Vagina", "aliases": ["pussy", "vulva"]},
]

# Free tag boxes per frame (capacity > taxonomy so e.g. nipples×2 fits).
MAX_REGIONS_PER_IMAGE = 12

IDS = [p["id"] for p in CANONICAL]

ROLE_TRACKED = "tracked"
ROLE_FIXED = "fixed"
ROLE_MASK = "mask"

_ALIAS = {}
for _p in CANONICAL:
    _ALIAS[_p["id"]] = _p["id"]
    for _a in _p.get("aliases") or []:
        _ALIAS[_a.lower()] = _p["id"]


def normalize(name: str) -> str:
    s = (name or "").strip().lower().replace(" ", "_").replace("-", "_")
    if not s:
        return ""
    return _ALIAS.get(s, s)


def preferred_classes_csv() -> str:
    return ",".join(IDS)


def default_role(class_id: str) -> str:
    cid = normalize(class_id)
    if cid in ("penis", "glans", "hand_1", "hand_2"):
        return ROLE_TRACKED
    if cid in ("nipples", "mouth", "vagina", "breasts"):
        return ROLE_FIXED
    if cid == "face":
        return ROLE_MASK
    return ROLE_TRACKED


def is_canonical(class_id: str) -> bool:
    return normalize(class_id) in set(IDS)
