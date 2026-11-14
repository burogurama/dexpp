# Documentation sources

This folder holds the sources of the documentation site,
<https://burogurama.github.io/dexpp/>. The site is built from a release tag,
so it describes the latest release.

| Path | What |
| ---- | ---- |
| `*.md`, `guide/`, `reference/` | The pages. `mkdocs.yml` in the repository root lists them. |
| `examples/*.py` | The Python examples the guides show. |
| `examples/output/` | What each example section prints. The guides show these files. |
| `examples/check.py` | Runs the examples and compares their output with the saved one. CI runs it. |
| `examples/cpp/` | The C++ examples. The unit test build compiles and runs them. |
| `Doxyfile`, `cpp-mainpage.md` | Doxygen setup for the C++ and C reference. |
| `hooks.py` | Puts the version on the site and prepares the Python stubs for the reference. |
| `build.sh`, `requirements.txt` | Build the site. |

## Build the site

```bash
sudo apt-get install doxygen
python3 -m venv .venv-docs
.venv-docs/bin/pip install -r docs/requirements.txt
PATH=$PWD/.venv-docs/bin:$PATH docs/build.sh          # writes site/
PATH=$PWD/.venv-docs/bin:$PATH docs/build.sh --serve  # live preview
```

The build is strict. A Doxygen warning, a broken link or a missing example
file fails it.

## Where the text comes from

- **Guides** are written by hand in this folder.
- **The Python reference** is generated from the docstrings in
  `bindings/python/dexpp.pyi`. To change a description, edit the stub.
- **The C++ and C reference** is generated from the comments in the public
  headers under `src/` and `bindings/c/`. To change a description, edit the
  header.

## Examples

Guides do not contain example code. They include it from `examples/`, and they
include the output from `examples/output/`:

````markdown
```python
--8<-- "code.py:classes"
```

```text
--8<-- "output/code.classes.txt"
```
````

`code.py:classes` is the part of `examples/code.py` between the
`# --8<-- [start:classes]` and `# --8<-- [end:classes]` comments.

After changing an example, write its output again and look at the diff:

```bash
PYTHONPATH=build python3 docs/examples/check.py --update
git diff docs/examples/output
```

## Explicit types

Every code snippet on the site states its types:

- **Python**: every variable has an annotation
  (`ctx: dexpp.AnalysisContext = ...`), and every function has a typed
  signature. A value that can be `None` is annotated `X | None` and checked
  with `assert value is not None` before use. CI runs
  `MYPYPATH=bindings/python mypy --strict docs/examples/*.py`, so a wrong
  annotation fails the build.
- **C++**: the full type, not `auto`.

Loop variables and comprehension variables take their type from the annotated
collection they come from. The Go snippets keep Go's `:=`.
