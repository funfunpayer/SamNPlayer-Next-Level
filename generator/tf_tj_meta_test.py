"""Tests für das tf/tj-Rezept, ohne Video und ohne OpenCV."""

import tf_tj_meta as m


def test_is_distance_profile():
    assert m.is_distance_profile("tf")
    assert m.is_distance_profile("TJ")
    assert not m.is_distance_profile("weich")
    assert not m.is_distance_profile("")


def test_device_recipe():
    r = m.device_recipe_for("tj")
    assert r["sync"] == "suction_position"
    assert r["min_suction"] == 0.20
    assert "contact_vibration" not in r
    assert m.device_recipe_for("standard") is None


def test_device_recipe_contact_vibration():
    r = m.device_recipe_for("tj", contact_vibration=True)
    assert r["contact_vibration"] is True
    # Default-Span/Kurve nicht mitschreiben - ältere Player brauchen nichts.
    assert "contact_vibration_span" not in r
    assert "contact_vibration_curve" not in r
    # Nicht angefordert -> Feld fehlt ganz, statt explizit False zu sein -
    # ältere Player, die das Feld nicht kennen, sollen nichts lesen müssen.
    assert "contact_vibration" not in m.device_recipe_for("tj", contact_vibration=False)
    # Stroke profiles: recipe only when contact vib is on.
    stroke = m.device_recipe_for("standard", contact_vibration=True)
    assert stroke is not None
    assert stroke["sync"] == "independent"
    assert stroke["contact_vibration"] is True
    assert m.device_recipe_for("standard", contact_vibration=False) is None
    weich = m.device_recipe_for("weich", contact_vibration=True)
    assert weich["sync"] == "independent"
    assert weich["contact_vibration"] is True
    auto = m.device_recipe_for("autotune", contact_vibration=True)
    assert auto["contact_vibration"] is True


def test_device_recipe_contact_span_and_curve():
    r = m.device_recipe_for("tj", contact_vibration=True,
                           contact_vibration_span=0.5,
                           contact_vibration_curve="soft")
    assert r["contact_vibration"] is True
    assert r["contact_vibration_span"] == 0.5
    assert r["contact_vibration_curve"] == "soft"
    # Default-Werte bleiben weg.
    r2 = m.device_recipe_for("tj", contact_vibration=True,
                            contact_vibration_span=0.75,
                            contact_vibration_curve="linear")
    assert "contact_vibration_span" not in r2
    assert "contact_vibration_curve" not in r2


def test_clamp_contact_span():
    assert m.clamp_contact_span(None) == 0.75
    assert m.clamp_contact_span(0) == 0.75
    assert m.clamp_contact_span(0.5) == 0.5
    assert m.clamp_contact_span(0.2) == 0.4
    assert m.clamp_contact_span(0.99) == 0.95


def test_clamp():
    out = m.clamp_actions_pos([{"at": 0, "pos": 0}, {"at": 1, "pos": 100}])
    assert out[0]["pos"] == 20
    assert out[1]["pos"] == 90


def test_metadata():
    meta = m.apply_profile_metadata({"creator": "x"}, "tf")
    assert meta["profile"] == "tj"
    assert meta["device_recipe"]["sync"] == "suction_position"
    assert "contact_vibration" not in meta["device_recipe"]


def test_metadata_contact_vibration():
    meta = m.apply_profile_metadata({"creator": "x"}, "tj", contact_vibration=True)
    assert meta["device_recipe"]["contact_vibration"] is True


def test_metadata_stroke_contact_vibration():
    meta = m.apply_profile_metadata(
        {"creator": "x"}, "standard", contact_vibration=True,
        contact_vibration_curve="soft")
    assert meta["profile"] == "standard"
    assert meta["device_recipe"]["sync"] == "independent"
    assert meta["device_recipe"]["contact_vibration"] is True
    assert meta["device_recipe"]["contact_vibration_curve"] == "soft"
    # Without contact vib, standard stays recipe-free.
    bare = m.apply_profile_metadata({"creator": "x"}, "standard")
    assert "device_recipe" not in bare


def test_metadata_contact_curve():
    meta = m.apply_profile_metadata(
        {"creator": "x"}, "tj", contact_vibration=True,
        contact_vibration_span=0.55, contact_vibration_curve="peak")
    assert meta["device_recipe"]["contact_vibration_span"] == 0.55
    assert meta["device_recipe"]["contact_vibration_curve"] == "peak"


def test_normalize_contact_curve_impulse():
    assert m.normalize_contact_curve("impulse") == "impulse"
    assert m.normalize_contact_curve("events") == "impulse"
    assert m.normalize_contact_curve("peak_emphasis") == "impulse"
    r = m.device_recipe_for("standard", contact_vibration=True,
                           contact_vibration_curve="impulse")
    assert r["contact_vibration_curve"] == "impulse"


if __name__ == "__main__":
    test_is_distance_profile()
    test_device_recipe()
    test_device_recipe_contact_vibration()
    test_device_recipe_contact_span_and_curve()
    test_clamp_contact_span()
    test_clamp()
    test_metadata()
    test_metadata_contact_vibration()
    test_metadata_stroke_contact_vibration()
    test_metadata_contact_curve()
    test_normalize_contact_curve_impulse()
    print("ok")
