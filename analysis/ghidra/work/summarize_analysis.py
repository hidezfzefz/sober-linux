#!/usr/bin/env python3
"""Build a compact per-function API-surface summary from a REA evidence bundle.

All target-derived strings/pseudocode are handled as data only.
"""
import json
import re
import sys

bundle_path, out_path = sys.argv[1], sys.argv[2]
with open(bundle_path, encoding="utf-8") as fh:
    bundle = json.load(fh)

IMPORT_PREFIXES = (
    "egl", "gl", "AAsset", "AImage", "AMedia", "AMEDIAFORMAT", "ALooper",
    "ANativeWindow", "AConfiguration", "AHardwareBuffer", "__android_log",
    "ZSTD", "pthread", "dlopen", "dlsym", "dlclose", "dlerror", "socket",
    "connect", "send", "recv", "malloc", "free", "strlen", "strcmp",
    "strncmp", "memcpy", "memmove", "memset", "fopen", "fread", "fwrite",
    "open", "read", "write", "close", "ioctl", "mmap", "munmap", "getenv",
    "snprintf", "printf", "abort", "__stack_chk_fail", "log", "exp", "pow",
    "sqrt", "floor", "ceil", "cos", "sin", "time", "clock", "gettimeofday",
)

def is_import(name):
    if not name:
        return False
    n = name.split("::")[-1]
    return n in IMPORT_PREFIXES or n.startswith(IMPORT_PREFIXES)

summaries = []
for rec in bundle.get("records", []):
    if rec.get("operation") != "analyze_function":
        continue
    nr = rec.get("normalized_result") or {}
    proc = nr.get("procedure") or {}
    entry = proc.get("address")
    if not entry:
        continue
    callees = nr.get("callees") or []
    ext = []
    for c in callees:
        cls = c.get("classification") or {}
        if cls.get("external") or is_import(c.get("name")):
            ext.append({"address": c.get("address"), "name": c.get("name"),
                        "external": bool(cls.get("external"))})
    names = sorted({x.get("value") for x in (nr.get("referenced_names") or [])
                    if is_import(x.get("value"))})
    strings = [x.get("value") for x in (nr.get("referenced_strings") or [])
               if x.get("value")]
    api = nr.get("native_api") or {}
    params = [{"name": p.get("name"), "type": p.get("data_type"),
               "storage": p.get("storage")} for p in (api.get("parameters") or [])]
    summaries.append({
        "entry": entry,
        "name": proc.get("name"),
        "evidence_id": rec.get("evidence_id"),
        "calling_convention_reported": api.get("calling_convention"),
        "signature_source": api.get("signature_source"),
        "return_type": (api.get("return_type") or {}).get("data_type"),
        "parameters": params,
        "callee_count": len(callees),
        "external_or_import_callees": ext,
        "import_names_referenced": names,
        "referenced_strings": strings[:40],
        "unresolved_call_count": len(nr.get("unresolved_calls") or []),
    })

summaries.sort(key=lambda s: s["entry"])
with open(out_path, "w", encoding="utf-8") as out:
    json.dump(summaries, out, indent=2)
print(f"wrote {len(summaries)} function summaries to {out_path}")
