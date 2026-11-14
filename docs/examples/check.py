"""Run every example in this folder and compare its output with the saved one.

The guides show the code of these files and their saved output, so a guide can
only show code that runs and output that is real.

An example is split into named sections by marker comments:

    # --8<-- [start:classes]
    ...code...
    # --8<-- [end:classes]

The guides include a section's code by that name. This script runs the example
one piece at a time and saves what each section prints to
output/<example>.<section>.txt, plus the whole output to output/<example>.txt.

Run from the repository root, with the dexpp module importable:

    PYTHONPATH=build python3 docs/examples/check.py            # check
    PYTHONPATH=build python3 docs/examples/check.py --update   # rewrite the saved output

An example that raises stops the run with its traceback.
"""

import contextlib
import io
import pathlib
import re
import sys
from typing import Any

_HERE: pathlib.Path = pathlib.Path(__file__).parent
_OUTPUT: pathlib.Path = _HERE / "output"
_MARKER: re.Pattern[str] = re.compile(r"^# --8<-- \[(start|end):([a-z_]+)\]$")


def _run_chunk(source: str, path: pathlib.Path, namespace: dict[str, Any]) -> str:
    """Run one piece of an example and return what it printed."""
    printed: io.StringIO = io.StringIO()
    with contextlib.redirect_stdout(printed):
        exec(compile(source, str(path), "exec"), namespace)
    return printed.getvalue()


def _run_example(path: pathlib.Path) -> dict[str, str]:
    """Run one example and return its output, keyed by output file name."""
    namespace: dict[str, Any] = {"__name__": "__main__", "__file__": str(path)}
    outputs: dict[str, str] = {}
    whole: list[str] = []
    section: str | None = None
    chunk: list[str] = []
    first_line: int = 1

    lines: list[str] = path.read_text(encoding="utf-8").splitlines(keepends=True)
    # A marker after the last line closes the last chunk.
    for number, line in enumerate([*lines, "# --8<-- [end:eof]\n"], start=1):
        marker: re.Match[str] | None = _MARKER.match(line.strip())
        if marker is None:
            chunk.append(line)
            continue
        # Blank lines in front keep the line numbers of a traceback right.
        source: str = "\n" * (first_line - 1) + "".join(chunk)
        printed: str = _run_chunk(source, path, namespace)
        whole.append(printed)
        if section is not None:
            outputs[f"{path.stem}.{section}.txt"] = printed
        section = marker.group(2) if marker.group(1) == "start" else None
        chunk = []
        first_line = number + 1

    outputs[f"{path.stem}.txt"] = "".join(whole)
    return outputs


def _is_saved(name: str, text: str) -> bool:
    """Whether output/<name> exists and holds exactly this text."""
    saved: pathlib.Path = _OUTPUT / name
    return saved.exists() is True and saved.read_text(encoding="utf-8") == text


def _update(produced: dict[str, str]) -> None:
    """Write the produced output files and delete the ones no section makes."""
    _OUTPUT.mkdir(exist_ok=True)
    for old in _OUTPUT.glob("*.txt"):
        if old.name not in produced:
            old.unlink()
    for name, text in produced.items():
        (_OUTPUT / name).write_text(text, encoding="utf-8")
    print(f"wrote {len(produced)} files to {_OUTPUT.relative_to(_HERE.parent.parent)}/")


def main() -> int:
    """Run the examples; check their output, or rewrite it with --update.

    Returns:
        0 when every example matches its saved output (or after an update),
        1 otherwise.
    """
    update: bool = "--update" in sys.argv[1:]
    examples: list[pathlib.Path] = sorted(
        path for path in _HERE.glob("*.py") if path.name != "check.py"
    )
    produced: dict[str, str] = {}
    failed: int = 0
    for example in examples:
        outputs: dict[str, str] = _run_example(example)
        produced.update(outputs)
        if update is True:
            continue
        stale: list[str] = [
            name for name, text in outputs.items() if _is_saved(name, text) is False
        ]
        if len(stale) > 0:
            print(f"FAIL {example.name}: output differs from the saved one: {', '.join(stale)}")
            failed += 1
        else:
            print(f"PASS {example.name}")

    if update is True:
        _update(produced)
        return 0

    leftover: list[str] = sorted(
        path.name for path in _OUTPUT.glob("*.txt") if path.name not in produced
    )
    if len(leftover) > 0:
        print(f"FAIL: saved output with no section behind it: {', '.join(leftover)}")
        failed += 1
    if failed > 0:
        print("Run with --update after checking that the new output is right.")
        return 1
    print(f"{len(examples)} examples passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
