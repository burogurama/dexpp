"""Use-case benchmark: dexpp vs androguard on the same APK.

Each "use case" is a realistic static-analysis problem. Every (library, use case)
pair is solved in a FRESH subprocess so the measurement reflects the true
single-shot cost of solving that problem from a cold start — including each
library's loading model (dexpp lazy, androguard eager). Both wall-clock time and
peak resident memory (ru_maxrss) are captured.

Usage:
    python bench/bench_usecases.py path/to.apk [--repeat 3]
    # internal: python bench/bench_usecases.py --worker <lib> <usecase> <apk>

Requires `dexpp` and `androguard` importable.
"""

import argparse
import json
import os
import resource
import subprocess
import sys
import time


# --------------------------------------------------------------------------- #
# Use cases. Each is (dexpp_solver, androguard_solver); both return an integer
# "answer" used as a cross-library sanity check. Each solver does its OWN minimal
# loading, so the cost reflects what it takes to solve *that* problem.
# --------------------------------------------------------------------------- #

# ---- dexpp solvers ----
def dx_manifest(path):
    import dexpp

    apk = dexpp.Apk.open(path)
    m = apk.manifest
    return len(m.package) + len(m.components) + len(m.permissions)


def dx_classes(path):
    import dexpp

    return len(dexpp.AnalysisContext.from_apk(path).classes)


def dx_methods(path):
    import dexpp

    ctx = dexpp.AnalysisContext.from_apk(path)
    return sum(1 for c in ctx.classes for _ in c.methods)


def dx_strings(path):
    import dexpp

    return len(dexpp.AnalysisContext.from_apk(path).strings)


def dx_disasm(path):
    import dexpp

    ctx = dexpp.AnalysisContext.from_apk(path)
    n = 0
    for c in ctx.classes:
        for m in c.methods:
            if m.has_code:
                n += len(m.instructions)
    return n


def dx_find_class(path):
    import dexpp

    ctx = dexpp.AnalysisContext.from_apk(path)
    # repeated point lookups
    names = [c.name for c in ctx.classes[:200]]
    hits = sum(1 for n in names if ctx.find_class(n) is not None)
    return hits


def dx_call_graph(path):
    import dexpp

    return len(dexpp.AnalysisContext.from_apk(path).call_graph.edges)


def dx_xrefs(path):
    import dexpp

    ctx = dexpp.AnalysisContext.from_apk(path)
    x = ctx.xrefs
    # who references the strings: total const-string referrer sites
    return sum(len(x.string_refs(s)) for s in x.referenced_strings)


def dx_api_scan(path):
    import dexpp

    ctx = dexpp.AnalysisContext.from_apk(path)
    # enumerate every call site (a full "scan the bytecode for calls" pass)
    return sum(len(m.calls) for c in ctx.classes for m in c.methods if m.has_code)


# ---- androguard solvers ----
def ag_manifest(path):
    from androguard.core.apk import APK

    a = APK(path)
    comps = (
        (a.get_activities() or [])
        + (a.get_services() or [])
        + (a.get_receivers() or [])
        + (a.get_providers() or [])
    )
    return len(a.get_package()) + len(comps) + len(a.get_permissions() or [])


def _ag_dexes(path):
    from androguard.core.apk import APK
    from androguard.core.dex import DEX

    return [DEX(d) for d in APK(path).get_all_dex()]


def _ag_analysis(path):
    from androguard.core.analysis.analysis import Analysis

    dx = Analysis()
    for d in _ag_dexes(path):
        dx.add(d)
    return dx


def ag_classes(path):
    return sum(len(list(d.get_classes())) for d in _ag_dexes(path))


def ag_methods(path):
    # Defined (encoded) methods, to match dexpp's ctx.classes[*].methods.
    return sum(
        len(list(c.get_methods())) for d in _ag_dexes(path) for c in d.get_classes()
    )


def ag_strings(path):
    return sum(len(list(d.get_strings())) for d in _ag_dexes(path))


def ag_disasm(path):
    n = 0
    for d in _ag_dexes(path):
        for c in d.get_classes():
            for m in c.get_methods():
                if m.get_code() is not None:
                    n += sum(1 for _ in m.get_instructions())
    return n


def ag_find_class(path):
    dexes = _ag_dexes(path)
    names = []
    for d in dexes:
        for c in d.get_classes():
            names.append(c.get_name())
            if len(names) >= 200:
                break
        if len(names) >= 200:
            break
    hits = 0
    for n in names:
        for d in dexes:
            if d.get_class(n) is not None:
                hits += 1
                break
    return hits


def ag_call_graph(path):
    dx = _ag_analysis(path)
    dx.create_xref()
    g = dx.get_call_graph()
    return g.number_of_edges()


def ag_xrefs(path):
    dx = _ag_analysis(path)
    dx.create_xref()
    total = 0
    for s in dx.get_strings():
        total += len(list(s.get_xref_from()))
    return total


def ag_api_scan(path):
    n = 0
    for d in _ag_dexes(path):
        for c in d.get_classes():
            for m in c.get_methods():
                if m.get_code() is None:
                    continue
                for ins in m.get_instructions():
                    if ins.get_name().startswith("invoke"):
                        n += 1
    return n


USE_CASES = {
    "manifest (pkg+components+perms)": (dx_manifest, ag_manifest),
    "count classes": (dx_classes, ag_classes),
    "count methods": (dx_methods, ag_methods),
    "decode string pool": (dx_strings, ag_strings),
    "disassemble all methods": (dx_disasm, ag_disasm),
    "find_class x200 (lookup)": (dx_find_class, ag_find_class),
    "whole-program call graph": (dx_call_graph, ag_call_graph),
    "string xref index": (dx_xrefs, ag_xrefs),
    "scan all call sites": (dx_api_scan, ag_api_scan),
}


def worker(lib, usecase, path):
    dx_fn, ag_fn = USE_CASES[usecase]
    fn = dx_fn if lib == "dexpp" else ag_fn
    t0 = time.perf_counter()
    answer = fn(path)
    dt = (time.perf_counter() - t0) * 1000.0
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0  # MB on Linux
    print("RESULT " + json.dumps({"ms": dt, "rss_mb": rss, "answer": int(answer)}))


def run_one(lib, usecase, path, repeat):
    best = None
    for _ in range(repeat):
        p = subprocess.run(
            [sys.executable, __file__, "--worker", lib, usecase, path],
            capture_output=True,
            text=True,
        )
        line = [x for x in p.stdout.splitlines() if x.startswith("RESULT ")]
        if not line:
            return None, None, "ERR:" + (p.stderr.strip().splitlines()[-1:] or [""])[0][:60]
        r = json.loads(line[0][len("RESULT "):])
        if best is None or r["ms"] < best["ms"]:
            best = r
    return best["ms"], best["rss_mb"], best["answer"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apk")
    ap.add_argument("--repeat", type=int, default=3)
    args = ap.parse_args()

    print(f"APK: {args.apk}  ({os.path.getsize(args.apk)/1e6:.1f} MB)  best-of-{args.repeat}, cold subprocess\n")
    hdr = f"{'use case':<34}{'dexpp ms':>10}{'androg ms':>11}{'speedup':>9}{'dx MB':>8}{'ag MB':>8}{'mem×':>7}  answers"
    print(hdr)
    print("-" * len(hdr))
    for name in USE_CASES:
        dms, drss, dans = run_one("dexpp", name, args.apk, args.repeat)
        ams, arss, aans = run_one("androguard", name, args.apk, args.repeat)
        if dms is None or ams is None:
            print(f"{name:<34}  {dms or ''} {ams or ''}  (error)")
            continue
        spd = ams / dms if dms else float("inf")
        memx = arss / drss if drss else float("inf")
        match = "=" if dans == aans else f"dx={dans} ag={aans}"
        print(
            f"{name:<34}{dms:>10.1f}{ams:>11.1f}{spd:>8.1f}×{drss:>8.0f}{arss:>8.0f}{memx:>6.1f}×  {match}"
        )


if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "--worker":
        worker(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        main()
