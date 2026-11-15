#pragma once

/** dexpp.AccessFlags, the Python view of dex::AccessFlags, shared by the two
 *  binding files.
 *
 *  It is an enum.IntFlag, so the flags read like names
 *  (`AccessFlags.PUBLIC in cls.access_flags`) and are still plain ints for
 *  code that tests bits by value. */

#include <pybind11/eval.h>
#include <pybind11/gil_safe_call_once.h>
#include <pybind11/pybind11.h>

#include <cstdint>

namespace dexpp_py {

namespace py = pybind11;

/** Define dexpp.AccessFlags in module @p m.  The values are those of
 *  dex::AccessFlags (src/common/access_flags.hpp); keep the two in step. */
inline void define_access_flags(py::module_ &m)
{
    py::exec(R"py(
import enum as _enum


class AccessFlags(_enum.IntFlag):
    """The access flags of a class, a method or a field, as the DEX format stores them."""

    PUBLIC = 0x0001
    PRIVATE = 0x0002
    PROTECTED = 0x0004
    STATIC = 0x0008
    FINAL = 0x0010
    SYNCHRONIZED = 0x0020
    VOLATILE = 0x0040
    BRIDGE = 0x0040
    TRANSIENT = 0x0080
    VARARGS = 0x0080
    NATIVE = 0x0100
    INTERFACE = 0x0200
    ABSTRACT = 0x0400
    STRICT = 0x0800
    SYNTHETIC = 0x1000
    ANNOTATION = 0x2000
    ENUM = 0x4000
    CONSTRUCTOR = 0x10000
    DECLARED_SYNCHRONIZED = 0x20000


del _enum
)py",
             m.attr("__dict__"));
}

/** The flags @p value as a dexpp.AccessFlags. */
inline py::object access_flags_to_py(uint32_t value)
{
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<py::object> type;
    py::object &cls = type.call_once_and_store_result(
                              [] { return py::module_::import("dexpp").attr("AccessFlags"); })
                          .get_stored();
    return cls(value);
}

} // namespace dexpp_py
