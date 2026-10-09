#pragma once

/** dex++ library version.
 *
 *  This is the single source of truth for the version: CMake reads it into
 *  project(VERSION) and scikit-build-core reads it into the Python package
 *  metadata, so a release bumps only these lines. */

#define DEXPP_VERSION_MAJOR 0
#define DEXPP_VERSION_MINOR 1
#define DEXPP_VERSION_PATCH 0
#define DEXPP_VERSION "0.1.0"
