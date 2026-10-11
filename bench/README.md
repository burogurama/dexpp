# Benchmarks

dexpp vs [androguard](https://github.com/androguard/androguard) on the same
APK, same machine, best-of-5 wall-clock.

## Reproduce

```bash
bash bench/gen_bench_apk.sh 400            # -> tests/data/bench.apk (gitignored)
pip install .                              # install the dexpp wheel
pip install androguard                     # the comparison target
python bench/bench_vs_androguard.py --repeat 5
```

## Result (400-class APK, ~3500 methods)

| operation            | dexpp (ms) | androguard (ms) | speedup |
| -------------------- | ---------: | --------------: | ------: |
| **END-TO-END (cold)**|     **2.6**|       **223.6** | **86×** |
| load + parse         |        0.2 |           214.0 |   ~1000× |
| call graph           |        0.4 |            20.0 |     57× |
| manifest             |       0.01 |             2.6 |    300× |

(Numbers vary by machine; rerun locally for your own.)

## Reading the numbers

The **END-TO-END (cold)** row is the fair headline: from a fresh load, run a
realistic triage pass — parse, enumerate every method, collect all strings, and
build the whole-program call graph.

The isolated rows need context because the two libraries make opposite
load-time choices:

- **androguard parses eagerly.** Its `load + parse` (which includes
  `create_xref()`) does essentially all the work up front, so later
  re-enumeration of already-built Python objects is near-instant.
- **dexpp parses lazily.** `Apk.open()` + `analysis()` defer almost everything;
  the cost shows up on the first real query. So dexpp's isolated `load` looks
  ~1000× faster and its isolated re-enumeration can look *slower* than
  androguard's cached iteration — neither is the honest comparison in
  isolation.

The cold end-to-end total cancels both effects: doing the same real work from
scratch, dexpp is ~86× faster here, and it never pays for analyses the caller
doesn't ask for.
