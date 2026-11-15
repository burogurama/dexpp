#pragma once

// Helpers for the C API tests.  The tests call the API the way a C program
// does, and free every allocation with the function the header names for it,
// so the sanitizer run reports a leak, a double free or a use after free in
// the shim.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "dexpp.h"

namespace c_test {

/** Copy a string the C API returned and free it. */
inline std::string take(char *s)
{
    EXPECT_NE(s, nullptr);
    std::string copy = s == nullptr ? "" : s;
    dexpp_string_free(s);
    return copy;
}

/** Copy a string array the C API returned and free it. */
inline std::vector<std::string> take_all(char **strings, size_t count)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < count; ++i)
        out.emplace_back(strings[i]);
    dexpp_string_array_free(strings, count);
    return out;
}

/** Call @p fn(&count) for a string array, then copy and free the result.
 *  (Not take_all(fn(&n), n): the order in which arguments are evaluated is
 *  unspecified, so `n` could be read before the call sets it.) */
template <typename Fn> std::vector<std::string> list(Fn &&fn)
{
    size_t count = 0;
    char **strings = fn(&count);
    return take_all(strings, count);
}

/** The text of a DexppText, with its NUL bytes. */
inline std::string str(const DexppText &t) { return std::string(t.data, t.length); }

/** The message of an error, or "" for none. */
inline std::string message(const DexppError *err)
{
    return err == nullptr ? std::string() : std::string(dexpp_error_message(err));
}

inline DexppContext *open_dex(const char *path)
{
    DexppError *err = nullptr;
    DexppContext *ctx = dexpp_context_from_dex(path, &err);
    EXPECT_NE(ctx, nullptr) << message(err);
    EXPECT_EQ(err, nullptr);
    dexpp_error_free(err);
    return ctx;
}

/** The method called @p name in class @p descriptor of @p ctx (the caller
 *  frees it), or NULL. */
inline DexppMethod *find_method(const DexppContext *ctx, const char *descriptor, const char *name)
{
    DexppClass *cls = dexpp_context_find_class(ctx, descriptor);
    if (cls == nullptr)
        return nullptr;
    size_t count = 0;
    DexppMethod **methods = dexpp_class_methods(cls, &count);
    DexppMethod *found = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (found == nullptr && take(dexpp_method_name(methods[i])) == name) {
            found = methods[i];
            methods[i] = nullptr; // keep it; the array free skips NULL handles
        }
    }
    dexpp_method_array_free(methods, count);
    dexpp_class_free(cls);
    return found;
}

} // namespace c_test
