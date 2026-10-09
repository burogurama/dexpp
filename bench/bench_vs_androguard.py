"""Benchmark dexpp against androguard on the same APK.

Usage:
    python bench/bench_vs_androguard.py [path/to.apk] [--repeat N]

Requires both `dexpp` and `androguard` importable. Generate the default APK
with `bench/gen_bench_apk.sh` first. Times are wall-clock, best-of-N.
"""

import argparse
import statistics
import sys
import time


def bench(fn, repeat):
    best = float("inf")
    result = None
    for _ in range(repeat):
        t0 = time.perf_counter()
        result = fn()
        best = min(best, time.perf_counter() - t0)
    return best, result


def run_dexpp(path, repeat):
    import dexpp

    out = {}

    # Cold end-to-end: a realistic triage pass from a fresh load. This is the
    # fair headline number — dexpp loads lazily, so isolated "load" timings
    # understate it and isolated re-enumeration timings overstate it.
    def cold():
        apk = dexpp.Apk.open(path)
        _ = apk.manifest.package
        ctx = apk.analysis()
        n_methods = sum(1 for c in ctx.classes for _ in c.methods)
        n_strings = len(ctx.strings)
        n_edges = len(ctx.call_graph.edges)
        return n_methods + n_strings + n_edges

    out["END-TO-END (cold)"], _ = bench(cold, repeat)

    def load():
        apk = dexpp.Apk.open(path)
        ctx = apk.analysis()
        return apk, ctx

    out["load + parse"], (apk, ctx) = bench(load, repeat)
    out["enumerate classes"], classes = bench(lambda: list(ctx.classes), repeat)
    out["enumerate methods"], _ = bench(
        lambda: [m.name for c in ctx.classes for m in c.methods], repeat
    )
    out["all strings"], _ = bench(lambda: list(ctx.strings), repeat)
    out["call graph"], _ = bench(lambda: len(ctx.call_graph.edges), repeat)
    out["xrefs index"], _ = bench(lambda: ctx.xrefs.empty, repeat)
    out["manifest"], _ = bench(lambda: dexpp.Apk.open(path).manifest.package, repeat)
    return out, len(classes)


def run_androguard(path, repeat):
    from androguard.core.apk import APK
    from androguard.core.dex import DEX
    from androguard.core.analysis.analysis import Analysis

    out = {}

    def build(path):
        apk = APK(path)
        dexes = [DEX(d) for d in apk.get_all_dex()]
        dx = Analysis()
        for d in dexes:
            dx.add(d)
        dx.create_xref()
        return apk, dx

    def cold():
        apk, dx = build(path)
        _ = apk.get_package()
        n_methods = sum(1 for c in dx.get_classes() for _ in c.get_methods())
        n_strings = len(list(dx.get_strings()))
        n_edges = dx.get_call_graph().number_of_edges()
        return n_methods + n_strings + n_edges

    out["END-TO-END (cold)"], _ = bench(cold, repeat)

    def load():
        return build(path)

    out["load + parse"], (apk, dx) = bench(load, repeat)
    out["enumerate classes"], classes = bench(lambda: list(dx.get_classes()), repeat)
    out["enumerate methods"], _ = bench(
        lambda: [m.name for c in dx.get_classes() for m in c.get_methods()], repeat
    )
    out["all strings"], _ = bench(lambda: list(dx.get_strings()), repeat)
    # androguard builds xrefs during create_xref(); approximate the equivalent
    # whole-program passes:
    out["call graph"], _ = bench(lambda: dx.get_call_graph().number_of_edges(), repeat)
    out["xrefs index"], _ = bench(lambda: len(list(dx.get_strings())), repeat)
    out["manifest"], _ = bench(lambda: APK(path).get_package(), repeat)
    return out, len(classes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apk", nargs="?", default="tests/data/bench.apk")
    ap.add_argument("--repeat", type=int, default=5)
    args = ap.parse_args()

    try:
        d_times, d_classes = run_dexpp(args.apk, args.repeat)
    except ImportError:
        print("dexpp not importable; install the wheel first", file=sys.stderr)
        return 1

    a_times = None
    try:
        a_times, a_classes = run_androguard(args.apk, args.repeat)
    except Exception as e:  # androguard optional / version-fragile
        print(f"(androguard unavailable: {e})\n", file=sys.stderr)

    print(f"APK: {args.apk}   classes(dexpp)={d_classes}   best-of-{args.repeat}\n")
    header = f"{'operation':<22}{'dexpp (ms)':>14}"
    if a_times:
        header += f"{'androguard (ms)':>18}{'speedup':>10}"
    print(header)
    print("-" * len(header))
    for op in d_times:
        line = f"{op:<22}{d_times[op] * 1e3:>14.2f}"
        if a_times and op in a_times:
            ag = a_times[op] * 1e3
            line += f"{ag:>18.2f}{ag / (d_times[op] * 1e3):>9.1f}x"
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
