"""The student's dataset step (no RF-DETR needed): class names merged across
clips, label ids renumbered, whole clips held out for validation."""

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import train_student  # noqa: E402


def clip(root: Path, name: str, classes: list[str], frames: int, label: str) -> Path:
    d = root / name
    (d / "images").mkdir(parents=True)
    (d / "labels").mkdir()
    (d / "classes.txt").write_text("\n".join(classes) + "\n", encoding="utf-8")
    for i in range(frames):
        (d / "images" / f"{name}_{i:04d}.jpg").write_bytes(b"jpg")
        if label is not None:
            (d / "labels" / f"{name}_{i:04d}.txt").write_text(label, encoding="utf-8")
    return d


def test_merge_and_split() -> None:
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        a = clip(root, "a", ["glans", "hand"], 4, "1 0.5 0.5 0.1 0.1\n")
        b = clip(root, "b", ["hand"], 3, "0 0.2 0.2 0.1 0.1\n")
        c = clip(root, "c", ["mouth", "glans"], 2, "")  # a negative clip
        names, n_train, n_valid = train_student.build_dataset([a, b, c], root / "work")
        assert names == ["glans", "hand", "mouth"], names
        # 3 clips, a fifth held out: the last clip is the validation part.
        assert (n_train, n_valid) == (7, 2), (n_train, n_valid)
        train_labels = sorted((root / "work" / "train" / "labels").iterdir())
        ids = {p.read_text().split()[0] for p in train_labels if p.read_text().strip()}
        assert ids == {"1"}, ids  # every "hand", from a's 1 and b's 0
        yaml = (root / "work" / "data.yaml").read_text()
        assert "nc: 3" in yaml and '  - "mouth"' in yaml, yaml


def test_one_clip_keeps_its_last_fifth() -> None:
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        a = clip(root, "a", ["glans"], 10, "0 0.5 0.5 0.1 0.1\n")
        _, n_train, n_valid = train_student.build_dataset([a], root / "work")
        assert (n_train, n_valid) == (8, 2)


def test_too_few_samples_say_so() -> None:
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        a = clip(root, "a", ["glans"], 1, "0 0.5 0.5 0.1 0.1\n")
        try:
            train_student.build_dataset([a], root / "work")
        except ValueError as e:
            assert "too few" in str(e)
        else:
            raise AssertionError("one sample cannot be split")


def test_errors_are_one_json_line(capsys=None) -> None:
    with tempfile.TemporaryDirectory() as t:
        import io
        from contextlib import redirect_stdout

        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = train_student.main(["--data", str(Path(t) / "missing"), "--out", str(Path(t) / "out")])
        assert rc == 1
        msg = json.loads(buf.getvalue().strip().splitlines()[-1])
        assert msg["event"] == "error", msg


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
