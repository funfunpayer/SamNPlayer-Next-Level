#!/usr/bin/env python3
"""Counts the PARITY.md checklist per section: done ([x]), partly (an open
item with a "partly" note), dropped on purpose (replaced / dropped) and open.
The share counts partly as half. Run: python3 ci/parity_status.py [docs/PARITY.md]"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "docs/PARITY.md"
rows, order, sec = {}, [], None
with open(path, encoding="utf-8") as f:
    for line in f:
        if line.startswith("## "):
            sec = line[3:].strip()
            order.append(sec)
            rows[sec] = [0, 0, 0, 0]
            continue
        m = re.match(r"\s*- \[( |x)\]", line)
        if not m or sec is None:
            continue
        r = rows[sec]
        if m.group(1) == "x":
            r[0] += 1
        elif "_(replaced" in line or "~~" in line or "(dropped" in line:
            r[2] += 1
        elif "partly" in line:
            r[1] += 1
        else:
            r[3] += 1

total = [0, 0, 0, 0]
print("| Section | Done | Partly | Dropped on purpose | Open | Share |")
print("|---|---|---|---|---|---|")
for s in order:
    r = rows[s]
    n = sum(r)
    if n == 0:
        continue
    name = re.sub(r"\s*[(`].*$", "", s)
    print(f"| {name} | {r[0]} | {r[1]} | {r[2]} | {r[3]} | {round(100 * (r[0] + r[2] + 0.5 * r[1]) / n)} % |")
    total = [a + b for a, b in zip(total, r)]
n = sum(total)
print(f"| **All {n}** | {total[0]} | {total[1]} | {total[2]} | {total[3]} | **{round(100 * (total[0] + total[2] + 0.5 * total[1]) / n)} %** |")
