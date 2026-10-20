"""A stand-in for ffmpeg in the tests (ClipPrepController): notes its
arguments in FAKE_FFMPEG_LOG, prints -progress lines for the -t length and
writes a small file to the last argument. FAKE_FFMPEG_FAIL fails after a
partial file; FAKE_FFMPEG_SLOW takes its time so a test can stop it."""
import json
import os
import sys
import time

args = sys.argv[1:]
if os.environ.get("FAKE_FFMPEG_LOG"):
    with open(os.environ["FAKE_FFMPEG_LOG"], "a", encoding="utf-8") as f:
        f.write(json.dumps(args) + "\n")
out = args[-1]
length = float(args[args.index("-t") + 1])
with open(out, "wb") as f:
    f.write(b"partial")
steps = 4
for i in range(1, steps + 1):
    time.sleep(2.0 if os.environ.get("FAKE_FFMPEG_SLOW") else 0.02)
    us = int(length * 1e6 * i / steps)
    print(f"frame={i * 10}\nout_time_us={us}\nout_time_ms={us}\nprogress={'end' if i == steps else 'continue'}", flush=True)
    if os.environ.get("FAKE_FFMPEG_FAIL") and i == 2:
        print(f"{args[args.index('-i') + 1]}: Invalid data found when processing input", file=sys.stderr, flush=True)
        sys.exit(1)
with open(out, "wb") as f:
    f.write(b"fake mp4 " + json.dumps(args).encode())
