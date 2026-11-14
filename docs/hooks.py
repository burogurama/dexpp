"""MkDocs hooks for the dex++ site.

Two jobs, both done before any page is read:

* Put the library version on the site. It is read from src/version.hpp, the
  single place it is defined. The site is built from a release tag, so this is
  the version the pages describe.
* Give the Python reference something it can read. The reference is generated
  from the type stubs (bindings/python/dexpp.pyi), but the tool only loads a
  stub file that sits inside a package folder. So the stub is copied to
  build-docs/stubs/dexpp/__init__.pyi, where mkdocs.yml points.
"""

import pathlib
import re
import shutil

from mkdocs import plugins
from mkdocs.config import defaults
from mkdocs.structure import files as structure_files
from mkdocs.structure import pages

_VERSION_PLACEHOLDER: str = "{{ dexpp_version }}"
_VERSION_DEFINE: re.Pattern[str] = re.compile(r'#define DEXPP_VERSION "([^"]+)"')


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
    """Replace the version placeholder in a page with the library version.

    Args:
        markdown: The page's Markdown source.
        page: The page being read.
        config: The site configuration.
        files: Every file of the site.

    Returns:
        The Markdown source with the placeholder replaced.
    """
    return markdown.replace(_VERSION_PLACEHOLDER, config.extra["dexpp_version"])
