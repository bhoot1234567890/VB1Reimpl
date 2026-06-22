#!/usr/bin/env python3
"""
Generate web/excitation-tables.js from Source/VB1ExcitationTables.h.

Emits an ES module exporting:
  EXC_TABLES    -> Float32Array[11]   (the 11 real 4096-point tables)
  PROGRAM_TABLE -> number[16]         (program -> table index)

Regenerate after editing the C++ tables:
    python tools/gen_web.py
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "Source" / "VB1ExcitationTables.h"
OUT = ROOT / "web" / "excitation-tables.js"

# From VB1ExcitationTables.h excitationTable().
PROGRAM_TABLE = [0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 1, 6, 7, 8, 9, 10]


def main() -> None:
    text = HEADER.read_text()
    tables: dict[int, list[float]] = {}
    for m in re.finditer(r"exc_table_(\d+)\[4096\]\s*=\s*\{([^}]*)\}", text, re.S):
        idx = int(m.group(1))
        nums = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", m.group(2))
        tables[idx] = [float(x) for x in nums[:4096]]

    if len(tables) != 11:
        raise SystemExit(f"expected 11 tables, parsed {len(tables)}")

    out: list[str] = [
        "// Auto-generated from Source/VB1ExcitationTables.h -- do not edit by hand.",
        f"// {len(tables)} tables x 4096 floats, dumped from the original VB-1 binary.",
        "// Regenerate with:  python tools/gen_web.py",
        "",
        "export const PROGRAM_TABLE = [" + ", ".join(map(str, PROGRAM_TABLE)) + "];",
        "",
        "export const EXC_TABLES = [",
    ]
    for idx in sorted(tables):
        vals = tables[idx]
        chunks = []
        for i in range(0, len(vals), 8):
            chunks.append("      " + ", ".join(repr(v) for v in vals[i:i + 8]))
        out.append("  new Float32Array([")
        out.append(",\n".join(chunks))
        out.append(f"  ]),  // table {idx}")
    out.append("];")
    out.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(out))
    print(f"wrote {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes, {len(tables)} tables)")


if __name__ == "__main__":
    main()
