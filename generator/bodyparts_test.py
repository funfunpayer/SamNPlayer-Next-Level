"""bodyparts taxonomy — keep in sync with generator/bodyparts (Go)."""

import bodyparts as bp


def test_canonical_count():
    assert len(bp.CANONICAL) == 9
    assert bp.MAX_REGIONS_PER_IMAGE >= len(bp.CANONICAL) + 1  # free tags / nipples×2


def test_normalize_aliases():
    assert bp.normalize("Brust") == "breasts"
    assert bp.normalize("eichel") == "glans"
    assert bp.normalize("Hand") == "hand_1"
    assert bp.normalize("hand_left") == "hand_1"
    assert bp.normalize("hand_right") == "hand_2"
    assert bp.normalize("Mund") == "mouth"
    assert bp.normalize("vagina") == "vagina"
    assert bp.normalize("custom_toy") == "custom_toy"


def test_preferred_csv_has_mouth_and_vagina():
    csv = bp.preferred_classes_csv()
    assert "mouth" in csv and "vagina" in csv and "nipples" in csv


def test_default_roles():
    assert bp.default_role("glans") == bp.ROLE_TRACKED
    assert bp.default_role("nipples") == bp.ROLE_FIXED
    assert bp.default_role("face") == bp.ROLE_MASK


if __name__ == "__main__":
    test_canonical_count()
    test_normalize_aliases()
    test_preferred_csv_has_mouth_and_vagina()
    test_default_roles()
    print("OK bodyparts")
