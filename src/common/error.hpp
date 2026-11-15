#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dex {

/** What went wrong in a call that failed.  Every function of the library that
 *  can fail returns a std::expected with an Error, and Error::code is one of
 *  these.  The names match the exceptions of the Python binding (InvalidDex
 *  is InvalidDexError) and the error codes of the C API (InvalidDex is
 *  DEXPP_ERROR_INVALID_DEX). */
enum class ErrorCode : uint8_t {
    /** The path does not exist. */
    FileNotFound,
    /** The path exists but cannot be read: a directory, no permission, an
     *  I/O error. */
    CannotRead,
    /** The file cannot be created or written: a missing folder, a directory,
     *  no permission, a full disk. */
    CannotWrite,
    /** Not a ZIP archive, a damaged entry, or an APK with no classes.dex. */
    InvalidApk,
    /** A DEX file that cannot be parsed. */
    InvalidDex,
    /** A DEX file the editable model does not read: a DEX 041 container or a
     *  big-endian file. */
    UnsupportedDex,
    /** A model breaks rules of the DEX format.  Error::problems lists every
     *  rule it breaks. */
    InvalidModel,
    /** Part of the DEX file to write would be bigger than the format can
     *  encode: a table with too many entries (more than 65535 types, for
     *  example), a method whose code is too long, or too many try ranges or
     *  exception handlers in one method. */
    PoolOverflow,
    /** A model uses a feature that its DEX version does not have. */
    VersionTooLow,
    /** Not an Android binary XML document, or a damaged one
     *  (axml::parse). */
    InvalidXml,
    /** Not a compiled resource table (resources.arsc), or a damaged one
     *  (apk::ResourceTable::parse). */
    InvalidResources,
};

/** One rule of the DEX format that a model breaks, and where.  model::validate()
 *  returns a list of them, and an InvalidModel error carries the same list. */
struct Problem
{
    std::string where;   ///< e.g. "Lcom/a/B;->run()V, item 12"; empty for the whole file.
    std::string message; ///< What is wrong.
};

/** Why a call failed.  The same type comes back from every part of the
 *  library: the analysis and APK loaders, the binary XML and resource table
 *  parsers, and the editable model.  So one function can load an APK, read
 *  a DEX file into a model and write it again, and return a single
 *  std::expected<T, Error>.
 *
 *  The library does not throw for bad input; it returns an Error.  A lookup
 *  that finds nothing is not an error: it returns an empty result or
 *  std::nullopt. */
struct Error
{
    ErrorCode code;
    /** What failed, in words, with the path or the archive entry when there
     *  is one. */
    std::string message;
    /** For ErrorCode::InvalidModel: every problem model::validate() found.
     *  Empty for every other code. */
    std::vector<Problem> problems = {};
};

} // namespace dex
