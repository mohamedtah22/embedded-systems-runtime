"""Generate deterministic, SYNTHETIC fab test events (not Intel production data)."""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import random

TOOLS = (("ETCH-01", "etch", 0.976), ("ETCH-02", "etch", 0.875),
         ("LITHO-01", "lithography", 0.967), ("LITHO-02", "lithography", 0.974),
         ("CMP-01", "polish", 0.982))
FIELDS = ("event_id", "timestamp", "lot_id", "wafer_id", "tool_id", "process_step",
          "dies_tested", "dies_passed", "cycle_seconds", "temperature_c")


def generate(output: Path, notes_file: Path, lots: int = 50, wafers: int = 24, seed: int = 20261013) -> dict:
    if not 1 <= lots <= 100000 or not 1 <= wafers <= 1000:
        raise ValueError("lots and wafers must be positive and bounded")
    rng = random.Random(seed)
    output.parent.mkdir(parents=True, exist_ok=True)
    notes_file.parent.mkdir(parents=True, exist_ok=True)
    t0 = datetime(2026, 9, 1, tzinfo=timezone.utc)
    with output.open('w', newline='', encoding='utf-8') as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        for lot_no in range(lots):
            tool, step, pass_rate = TOOLS[lot_no % len(TOOLS)]
            lot = f"LOT-{lot_no+1:04d}"
            for wafer in range(wafers):
                count = rng.randrange(110, 161)
                passed = sum(rng.random() < pass_rate for _ in range(count))
                writer.writerow(dict(event_id=f"EVT-{lot_no+1:04d}-{wafer+1:03d}",
                                     timestamp=(t0 + timedelta(minutes=20 * (lot_no * wafers + wafer))).isoformat(),
                                     lot_id=lot, wafer_id=f"W{wafer+1:03d}", tool_id=tool,
                                     process_step=step, dies_tested=count, dies_passed=passed,
                                     cycle_seconds=round(rng.uniform(25, 75), 2),
                                     temperature_c=round(rng.uniform(18, 32), 2)))
    with notes_file.open('w', encoding='utf-8') as fh:
        for lot_no in range(lots):
            if lot_no % 7 != 1 and lot_no % 13 != 0:
                continue
            lot = f"LOT-{lot_no+1:04d}"
            bad = lot_no % len(TOOLS) == 1
            message = ("Defect spike after etch; engineer requested calibration check" if bad else
                       "Routine calibration verified; no equipment deviation")
            fh.write(json.dumps({'lotId':lot,'author':'synthetic-technician',
                                 'text':message,'source':'demo-only'},sort_keys=True)+'\n')
    return {"synthetic": True, "events": lots*wafers, "lots": lots, "seed": seed}

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--output',type=Path,default=Path('data/events.csv'))
    ap.add_argument('--notes',type=Path,default=Path('data/technician_notes.jsonl'))
    ap.add_argument('--lots',type=int,default=50)
    ap.add_argument('--wafers',type=int,default=24)
    ap.add_argument('--seed',type=int,default=20261013)
    a=ap.parse_args()
    print(json.dumps(generate(a.output,a.notes,a.lots,a.wafers,a.seed)))
