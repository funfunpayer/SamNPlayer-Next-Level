"""tf/tj-Rezept: Pos-Klammer und Metadata für generate_funscript.py.

tf und tj sind derselbe Abstand-Modus. Enge Distanz = hohe pos = starker Sog.
Stroke-Profile (standard / weich / autotune) können dieselbe
contact_vibration-Metadata tragen (Tiefe = hohe pos) — siehe
docs/TFTJ_PROFILE_DIRECTION.md. Playback liest metadata.profile /
metadata.device_recipe.
"""

TF_TJ_POS_MIN = 20
TF_TJ_POS_MAX = 90

DEFAULT_CONTACT_VIBRATION_SPAN = 0.75
CONTACT_CURVES = ("linear", "soft", "peak", "impulse")

# Profiles that get a device_recipe when contact vibration is on (stroke path).
_STROKE_PROFILES = ("", "standard", "hub", "weich", "autotune")


def is_distance_profile(name):
    return (name or "").strip().lower() in ("tf", "tj")


def is_stroke_profile(name):
    return (name or "").strip().lower() in _STROKE_PROFILES


def normalize_contact_curve(name):
    n = (name or "").strip().lower()
    if n in ("soft", "soft_entry", "weicher"):
        return "soft"
    if n in ("peak", "strong_peak", "peaky"):
        return "peak"
    if n in ("impulse", "events", "contact_events", "peak_emphasis"):
        return "impulse"
    return "linear"


def clamp_contact_span(span):
    """0 / None / ungültig → Default; sonst in [0.4, 0.95]."""
    if span is None:
        return DEFAULT_CONTACT_VIBRATION_SPAN
    try:
        v = float(span)
    except (TypeError, ValueError):
        return DEFAULT_CONTACT_VIBRATION_SPAN
    if v <= 0 or v >= 1:
        return DEFAULT_CONTACT_VIBRATION_SPAN
    return max(0.4, min(0.95, v))


def _contact_fields(contact_vibration, contact_vibration_span,
                    contact_vibration_curve):
    """Return contact_* keys to merge into a recipe, or {}."""
    if not contact_vibration:
        return {}
    out = {"contact_vibration": True}
    span = clamp_contact_span(contact_vibration_span)
    curve = normalize_contact_curve(contact_vibration_curve)
    # Only write non-defaults so older players stay small.
    if abs(span - DEFAULT_CONTACT_VIBRATION_SPAN) > 1e-9:
        out["contact_vibration_span"] = span
    if curve != "linear":
        out["contact_vibration_curve"] = curve
    return out


def device_recipe_for(profile, contact_vibration=False,
                      contact_vibration_span=None,
                      contact_vibration_curve=None):
    """Build device_recipe for distance profiles always; for stroke profiles
    only when contact_vibration is requested (so default stroke files stay
    recipe-free)."""
    contact = _contact_fields(
        contact_vibration, contact_vibration_span, contact_vibration_curve)

    if is_distance_profile(profile):
        recipe = {
            "sync": "suction_position",
            "min_suction": 0.20,
            "tick_ms": 50,
            "max_speed": 0.50,
            "smoothing": 0.22,
        }
        recipe.update(contact)
        return recipe

    if not contact_vibration:
        return None

    # Stroke / Normal / Autotune: independent sync (speed→vibe, pos→suction);
    # contact vib overlays the deep pos slice at playback.
    name = (profile or "standard").strip().lower()
    if name == "weich":
        recipe = {
            "sync": "independent",
            "min_vibration": 0.12,
            "min_suction": 0.10,
            "smoothing": 0.28,
        }
    else:
        recipe = {
            "sync": "independent",
            "smoothing": 0.3,
        }
    recipe.update(contact)
    return recipe


def clamp_actions_pos(actions, lo=TF_TJ_POS_MIN, hi=TF_TJ_POS_MAX):
    out = []
    for a in actions:
        pos = int(a["pos"])
        if pos < lo:
            pos = lo
        elif pos > hi:
            pos = hi
        out.append({"at": a["at"], "pos": pos})
    return out


def apply_profile_metadata(metadata, profile, contact_vibration=False,
                           contact_vibration_span=None,
                           contact_vibration_curve=None):
    """Ergänzt metadata um profile + device_recipe (distance always;
    stroke when contact_vibration is on)."""
    meta = dict(metadata)
    recipe = device_recipe_for(
        profile, contact_vibration,
        contact_vibration_span=contact_vibration_span,
        contact_vibration_curve=contact_vibration_curve)
    if is_distance_profile(profile):
        meta["profile"] = "tj"
        meta["device_recipe"] = recipe
    elif recipe is not None:
        meta["profile"] = (profile or "standard").strip().lower() or "standard"
        meta["device_recipe"] = recipe
    elif profile and profile != "standard":
        meta["profile"] = profile
    return meta
