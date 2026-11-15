#pragma once

#include <string>

namespace dex {

/** Why loading an APK or a DEX file failed.  Loading is the only thing in the
 *  analysis layer that reports an error: a lookup that finds nothing returns
 *  an empty result, and damage inside a file that did load gives empty
 *  results too. */
struct AnalysisError
{
    enum class Code {
        FileNotFound,   ///< Path does not exist.
        CannotRead,     ///< Path exists but cannot be read (a directory, no permission).
        InvalidDexFile, ///< A DEX file cannot be parsed.
        InvalidApk,     ///< Not a ZIP archive, or an APK with no classes.dex.
    };

    Code code;
    std::string message;
};

} // namespace dex
