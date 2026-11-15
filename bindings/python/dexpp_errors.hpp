#pragma once

/** The exceptions the dexpp Python module raises, shared by its two binding
 *  files.
 *
 *  The exception classes are ordinary Python classes, created once by
 *  define_error_classes() when the module is built:
 *
 *      Error(ValueError)
 *      ├── InvalidApkError
 *      ├── InvalidDexError
 *      ├── UnsupportedDexError
 *      └── ModelError
 *          ├── InvalidModelError    (has .problems)
 *          ├── PoolOverflowError
 *          └── VersionTooLowError
 *
 *  Error is a ValueError, so code that catches ValueError keeps working.  A
 *  file that cannot be opened is not a dexpp error: it raises the built-in
 *  FileNotFoundError or OSError. */

#include <pybind11/eval.h>
#include <pybind11/pybind11.h>

#include <string>

namespace dexpp_py {

namespace py = pybind11;

/** Define the exception classes in module @p m.  Call it first in the module
 *  init, before anything that can raise. */
inline void define_error_classes(py::module_ &m)
{
    // Python source, so the classes get docstrings, a normal __init__ and the
    // `problems` property without any C API boilerplate.  The module's own
    // dictionary is the namespace, which also makes their __module__ "dexpp".
    py::exec(R"py(
class Error(ValueError):
    """Base class of the errors dexpp raises for input it cannot use."""


class InvalidApkError(Error):
    """The file is not a ZIP archive, or the APK has no classes.dex."""


class InvalidDexError(Error):
    """A DEX file cannot be parsed."""


class UnsupportedDexError(Error):
    """A DEX file uses a format the editable model does not support."""


class ModelError(Error):
    """An operation on a Model failed."""


class InvalidModelError(ModelError):
    """A Model breaks rules of the DEX format; `problems` lists them."""

    def __init__(self, message="", problems=()):
        super().__init__(message)
        self._problems = list(problems)

    @property
    def problems(self):
        """Every broken rule, as a list of Problem."""
        return self._problems


class PoolOverflowError(ModelError):
    """A table of the DEX file would have more entries than the format allows."""


class VersionTooLowError(ModelError):
    """The Model uses a feature that its DEX version does not have."""
)py",
             m.attr("__dict__"));
}

/** Raise the dexpp exception class named @p type_name with @p message. */
[[noreturn]] inline void raise_error(const char *type_name, const std::string &message)
{
    py::object type = py::module_::import("dexpp").attr(type_name);
    PyErr_SetString(type.ptr(), message.c_str());
    throw py::error_already_set();
}

/** Raise for a file that could not be opened: FileNotFoundError. */
[[noreturn]] inline void raise_file_not_found(const std::string &message)
{
    PyErr_SetString(PyExc_FileNotFoundError, message.c_str());
    throw py::error_already_set();
}

/** Raise for a path that exists but could not be read (a directory, no
 *  permission), or a file that could not be created or written: OSError. */
[[noreturn]] inline void raise_os_error(const std::string &message)
{
    PyErr_SetString(PyExc_OSError, message.c_str());
    throw py::error_already_set();
}

} // namespace dexpp_py
