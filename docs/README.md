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
| `../bindings/go/dexpp/example_test.go` | The Go examples that `c-go.md` shows. `go test` runs them and checks their output. |
| `../bindings/java/src/test/java/.../ExamplesTest.java` | The Java examples that `java.md` shows. `mvn test` runs them and compares their output with `../bindings/java/src/test/resources/examples/`. |
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

The Go snippets of `c-go.md` work the same way. They come from the testable
examples in `bindings/go/dexpp/example_test.go`:

````markdown
```go
--8<-- "bindings/go/dexpp/example_test.go:load"
```
````

This is the part of the file between the `// --8<-- [start:load]` and
`// --8<-- [end:load]` comments. The output of each example is its
`// Output:` comment, which `go test` checks. Keep a blank line between the
end marker and `// Output:`. Without it, the two comments form one block that
does not start with "Output:", and `go test` only compiles the example
instead of running it.

The Java snippets of `java.md` come from the examples in
`bindings/java/src/test/java/io/github/burogurama/dexpp/ExamplesTest.java`,
between `// --8<-- [start:name]` and `// --8<-- [end:name]` comments. The
output each one prints is in
`bindings/java/src/test/resources/examples/NAME.txt`, which the page shows
too. The tests compare the two. After changing an example, write the output
files again and look at the diff:

```bash
cd bindings/java
mvn test -Dtest=ExamplesTest -Ddexpp.examples.update=true
git diff src/test/resources/examples
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
- **Java**: the full type, not `var`. Lambda parameters that a snippet shows
  have their type too: `(long sdk) -> ...`.

Loop variables and comprehension variables take their type from the annotated
collection they come from. The Go snippets keep Go's `:=`.
