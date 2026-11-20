#!/usr/bin/env python3
"""Extract Ghidra batch_decompile results from a REA evidence bundle into
per-function pseudocode files plus a compact index. Target-derived text is
treated strictly as data."""
import json
import os
import sys

bundle_path = sys.argv[1]
outdir = sys.argv[2]
index_path = sys.argv[3]
os.makedirs(outdir, exist_ok=True)

with open(bundle_path, encoding="utf-8") as fh:
    bundle = json.load(fh)

index = []
for rec in bundle.get("records", []):
    if rec.get("operation") != "batch_decompile":
        continue
    ev = rec.get("evidence_id")
    for item in rec.get("normalized_result", {}).get("items", []):
        if item.get("status") != "ok":
            index.append({"requested": item.get("address"), "status": item.get("status"), "evidence_id": ev})
            continue
        proc = item.get("procedure", {})
        name = proc.get("name") or "FUN_unknown"
        entry = proc.get("address") or item.get("address")
        safe = name.replace("/", "_")
        filename = f"{safe}_{entry.replace('0x','')}.c"
        path = os.path.join(outdir, filename)
        with open(path, "w", encoding="utf-8") as out:
            out.write(f"/* Ghidra decompilation of target routine {name} @ {entry}\n")
            out.write(f" * REA evidence: {ev}\n")
            out.write(f" * Requested address: {item.get('address')}\n")
            out.write(" * Target-derived data, not source. Do not execute.\n */\n\n")
            out.write(item.get("pseudocode") or "")
        index.append({
            "requested": item.get("address"),
            "entry": entry,
            "name": name,
            "file": filename,
            "pseudocode_bytes": len(item.get("pseudocode") or ""),
            "evidence_id": ev,
            "status": "ok",
        })

with open(index_path, "w", encoding="utf-8") as out:
    json.dump(index, out, indent=2)
print(f"wrote {len(index)} entries to {index_path}")
