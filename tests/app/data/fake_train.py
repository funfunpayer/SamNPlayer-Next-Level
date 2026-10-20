"""A stand-in for ai/train_student.py in the tests (TrainController): the
same JSON lines and files, no learning. FAKE_TRAIN_SLOW makes it wait for
STOP; FAKE_TRAIN_ERROR reports an error."""
import argparse
import json
import os
import shutil
import sys
import time
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--data", nargs="+")
ap.add_argument("--out")
ap.add_argument("--epochs", type=int)
ap.add_argument("--device")
a = ap.parse_args()
out = Path(a.out)


def say(event, **f):
    print(json.dumps({"event": event, **f}), flush=True)


if os.environ.get("FAKE_TRAIN_ERROR"):
    say("error", message="ValueError: too few samples")
    sys.exit(1)
say("start", device="cpu", train=8, valid=2, classes=["glans"], model="nano", data=a.data)
stopped = False
for epoch in range(1, a.epochs + 1):
    for step in range(1, 3):
        say("progress", epoch=epoch, epochs=a.epochs, step=step, steps=2)
        if os.environ.get("FAKE_TRAIN_SLOW"):
            for _ in range(200):
                if (out / "STOP").exists():
                    break
                time.sleep(0.05)
        if (out / "STOP").exists():
            stopped = True
            break
    say("epoch", epoch=epoch, epochs=a.epochs, map50=0.5)
    if stopped:
        break
shutil.copyfile(os.environ["FAKE_TRAIN_MODEL"], out / "student.onnx")
(out / "classes.json").write_text(json.dumps({"classes": ["glans"], "model": "nano"}))
say("done", model=str(out / "student.onnx"), classes=["glans"], stopped=stopped)
