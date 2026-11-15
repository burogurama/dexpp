# Documentation sources

This folder holds the sources of the documentation site,
<https://burogurama.github.io/dexpp/>. The site is built from a release tag,
so it describes the latest release.

| Path | What |
| ---- | ---- |
| `*.md`, `guide/`, `reference/` | The pages. `mkdocs.yml` in the repository root lists them. |
| `examples/*.py` | The Python examples the guides show. |
| `examples/output/` | What each Python example section prints. The guides show these files. |
| `examples/check.py` | Runs the Python examples and compares their output with the saved one. CI runs it. |
| `examples/cpp/` | The C++ examples. The unit test build compiles and runs them. |
| `../bindings/go/dexpp/example_*_test.go` | The Go examples the guides show. `go test` runs them and checks their output. |
| `../bindings/go/dexpp/testdata/examples/` | The output of each Go example, a copy of its `// Output:` comment. The guides show these files. |
| `../bindings/java/src/test/java/.../examples/` | The Java examples the guides show. `ExamplesTest` runs them. |
| `../bindings/java/src/test/resources/examples/` | What each Java example prints. `ExamplesTest` compares it, and the guides show these files. |
| `Doxyfile`, `cpp-mainpage.md` | Doxygen setup for the C++ and C reference. |
| `hooks.py` | Puts the version on the site, prepares the Python stubs for the reference, and turns each example line into Python, Go and Java tabs. |
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

Pages do not contain example code. Each example exists in three languages, in
files that the tests run, and a page includes the code and the output of all
three with one line:

```markdown
<!-- example: code.classes -->
```

`docs/hooks.py` turns this line into three content tabs, "Python", "Go" and
"Java". Each tab holds the code of the section `classes` of that language's
file for the topic `code`, and the output saved for that section:

| Language | Code | Output |
| -------- | ---- | ------ |
| Python | `docs/examples/code.py` | `docs/examples/output/code.classes.txt` |
| Go | `bindings/go/dexpp/example_code_test.go` | `bindings/go/dexpp/testdata/examples/code.classes.txt` |
| Java | `bindings/java/src/test/java/io/github/burogurama/dexpp/examples/CodeExamples.java` | `bindings/java/src/test/resources/examples/code.classes.txt` |

The section is the part of the file between the `--8<-- [start:classes]` and
`--8<-- [end:classes]` comments (`#` comments in Python, `//` comments in Go
and Java). The Java class name is the topic in CamelCase followed by
`Examples`. Several `topic.section` names can follow `example:`, separated by
spaces; each tab then shows all of them in that order. A missing file or
section fails the build. Choosing a language in one tab group switches every
group on the site (the `content.tabs.link` feature).

So a new example needs the same section in all three languages, with the same
topic and section name, and the three versions should print the same facts.
In Go and Java, the code before a section's start marker is not shown. It sets
up the variables that the earlier sections of the page made, since the Python
sections of one file share their variables.

### Python

`docs/examples/check.py` runs each Python example one section at a time and
saves what each section prints. After changing an example, write its output
again and look at the diff:

```bash
PYTHONPATH=build python3 docs/examples/check.py --update
git diff docs/examples/output
```

### Go

Each Go section is a testable example: one `func ExampleXxx()` with one start
marker in its body and an `// Output:` comment, which `go test` checks. Keep a
blank line between the end marker and `// Output:`. Without it, the two
comments form one block that does not start with "Output:", and `go test`
only compiles the example instead of running it.

`testdata/examples/TOPIC.SECTION.txt` holds a copy of the `// Output:` comment
for the site, and `TestDocsExampleOutputs` (in `docs_examples_test.go`) checks
that the copies match. After changing an example, write the copies again and
look at the diff:

```bash
cd bindings/go
go test ./dexpp -run TestDocsExampleOutputs -update
git diff dexpp/testdata/examples
```

### Java

Each Java section is a static method of the topic's class, listed in
`ExamplesTest`. The test runs each one and compares what it prints with
`src/test/resources/examples/TOPIC.SECTION.txt`. After changing an example,
write the output files again and look at the diff:

```bash
cd bindings/java
mvn test -Dtest=ExamplesTest -Ddexpp.examples.update=true
git diff src/test/resources/examples
```

The examples live in their own package, `io.github.burogurama.dexpp.examples`,
so they can only use the public API.

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
collection they come from. The Go snippets keep Go's `:=`, as Go code usually
does.
