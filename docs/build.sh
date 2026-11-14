#!/usr/bin/env bash
# Build the documentation site into site/.
#
#   docs/build.sh            build
#   docs/build.sh --serve    build the C++ reference, then serve the site at
#                            http://127.0.0.1:8000 and rebuild pages on change
#
# Needs doxygen and the Python packages in docs/requirements.txt:
#   python3 -m venv .venv-docs && .venv-docs/bin/pip install -r docs/requirements.txt
#   PATH=$PWD/.venv-docs/bin:$PATH docs/build.sh
#
# Two tools make the site:
#   1. Doxygen turns the C++ and C headers into docs/reference/cpp-api/.
#   2. MkDocs turns the Markdown pages and the Python stubs into site/, and
#      copies the Doxygen output along.
# Both run in strict mode: a Doxygen warning or a broken link fails the build.

set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

DEXPP_VERSION=$(sed -n 's/^#define DEXPP_VERSION "\(.*\)"$/\1/p' src/version.hpp)
export DEXPP_VERSION
# MkDocs Material prints a notice about its successor on every run.
export NO_MKDOCS_2_WARNING=1

echo "dex++ $DEXPP_VERSION: C++ and C reference (doxygen $(doxygen --version))"
rm -rf docs/reference/cpp-api
doxygen docs/Doxyfile

if [ "${1:-}" = "--serve" ]; then
    exec mkdocs serve
fi

echo "dex++ $DEXPP_VERSION: site"
mkdocs build --strict --site-dir site
echo "built site/index.html"
