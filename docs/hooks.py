"""MkDocs hooks for the dex++ site.

Three jobs:

* Put the library version on the site. It is read from src/version.hpp, the
  single place it is defined. The site is built from a release tag, so this is
  the version the pages describe.
* Give the Python reference something it can read. The reference is generated
  from the type stubs (bindings/python/dexpp.pyi), but the tool only loads a
  stub file that sits inside a package folder. So the stub is copied to
  build-docs/stubs/dexpp/__init__.pyi, where mkdocs.yml points.
* Turn an example line into three content tabs, one per language. A page
  writes

      <!-- example: code.classes -->

  and gets a "Python", a "Go" and a "Java" tab. Each tab shows the section
  "classes" of that language's example file for the topic "code", and the
  output saved for it. docs/README.md lists the files. More than one
  topic.section can follow "example:", separated by spaces; each tab then
  shows all of them, in that order. The tabs hold snippet lines
  (--8<-- "file:section"), so the snippets extension includes the files later,
  and a missing file or section fails the build.
"""

import pathlib
import re
import shutil
import typing

from mkdocs import plugins
from mkdocs.config import defaults
from mkdocs.structure import files as structure_files
from mkdocs.structure import pages

_VERSION_PLACEHOLDER: str = "{{ dexpp_version }}"
_VERSION_DEFINE: re.Pattern[str] = re.compile(r'#define DEXPP_VERSION "([^"]+)"')

_EXAMPLE_LINE: re.Pattern[str] = re.compile(
    r"^(?P<indent>[ ]*)<!-- example: (?P<names>[a-z_]+\.[a-z_]+(?: [a-z_]+\.[a-z_]+)*) -->[ ]*$",
    re.MULTILINE,
)


class _Language(typing.NamedTuple):
    """Where one language keeps its examples.

    The paths are snippet paths: the Python ones are relative to docs/examples,
    the others to the repository root (see base_path in mkdocs.yml). They are
    format strings with the fields topic, section and java_class.
    """

    label: str
    fence: str
    code: str
    output: str


_LANGUAGES: tuple[_Language, ...] = (
    _Language("Python", "python", "{topic}.py:{section}", "output/{topic}.{section}.txt"),
    _Language(
        "Go",
        "go",
        "bindings/go/dexpp/example_{topic}_test.go:{section}",
        "bindings/go/dexpp/testdata/examples/{topic}.{section}.txt",
    ),
    _Language(
        "Java",
        "java",
        "bindings/java/src/test/java/io/github/burogurama/dexpp/examples/{java_class}.java:{section}",
        "bindings/java/src/test/resources/examples/{topic}.{section}.txt",
    ),
)


def _example_tabs(match: re.Match[str]) -> str:
    """The content tabs for one example line.

    Args:
        match: The example line, matched by _EXAMPLE_LINE.

    Returns:
        A "Python", a "Go" and a "Java" tab, each with a code block and an
        output block made of snippet lines, indented like the example line.
    """
    names: list[tuple[str, str]] = []
    for name in match.group("names").split():
        topic, section = name.split(".")
        names.append((topic, section))

    lines: list[str] = []
    for language in _LANGUAGES:
        lines += [f'=== "{language.label}"', "", f"    ```{language.fence}"]
        for number, (topic, section) in enumerate(names):
            java_class: str = "".join(part.capitalize() for part in topic.split("_")) + "Examples"
            path: str = language.code.format(topic=topic, section=section, java_class=java_class)
            if number > 0:
                lines.append("")  # a blank line between two sections of code
            lines.append(f'    --8<-- "{path}"')
        lines += ["    ```", "", "    ```text"]
        for topic, section in names:
            path = language.output.format(topic=topic, section=section)
            lines.append(f'    --8<-- "{path}"')
        lines += ["    ```", ""]

    indent: str = match.group("indent")
    return "\n".join(indent + line if line != "" else "" for line in lines)


# Run before the plugins' own on_config: the reference plugin resolves its
# search path there, and the stub folder has to exist by then.
@plugins.event_priority(100)
def on_config(config: defaults.MkDocsConfig) -> defaults.MkDocsConfig:
    """Read the library version and copy the stub file into a package folder.

    Args:
        config: The site configuration, loaded from mkdocs.yml.

    Returns:
        The configuration, with the version in `extra` and in the site name.

    Raises:
        RuntimeError: src/version.hpp does not define DEXPP_VERSION.
    """
    root: pathlib.Path = pathlib.Path(config.config_file_path).parent

    header: str = (root / "src" / "version.hpp").read_text(encoding="utf-8")
    match: re.Match[str] | None = _VERSION_DEFINE.search(header)
    if match is None:
        raise RuntimeError("DEXPP_VERSION not found in src/version.hpp")
    version: str = match.group(1)
    config.extra["dexpp_version"] = version
    config.site_name = f"dex++ {version}"

    package: pathlib.Path = root / "build-docs" / "stubs" / "dexpp"
    package.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / "bindings" / "python" / "dexpp.pyi", package / "__init__.pyi")
    return config


def on_page_markdown(
    markdown: str,
    page: pages.Page,
    config: defaults.MkDocsConfig,
    files: structure_files.Files,
) -> str:
    """Expand the example lines and put the library version in a page.

    Args:
        markdown: The page's Markdown source.
        page: The page being read.
        config: The site configuration.
        files: Every file of the site.

    Returns:
        The Markdown source with each example line turned into content tabs and
        the version placeholder replaced.
    """
    markdown = _EXAMPLE_LINE.sub(_example_tabs, markdown)
    return markdown.replace(_VERSION_PLACEHOLDER, config.extra["dexpp_version"])
