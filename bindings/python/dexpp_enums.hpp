#pragma once

/** The enums of the dexpp Python module, and how they cross the boundary.
 *
 *  Each enum is a real Python enum.Enum (not an IntEnum), defined in Python
 *  source by define_enums(), so `list(dexpp.InvokeKind)`, `isinstance(k,
 *  enum.Enum)` and `k.name` / `k.value` work as for any Python enum.
 *
 *  A pybind11 type_caster (EnumCaster below) converts between each C++ enum
 *  and its Python class by value: a C++ value becomes the Python member with
 *  the same value, and a Python member becomes the C++ enumerator with its
 *  value.  So struct fields (CfgEdge.kind) and arguments
 *  (Manifest.components_of(kind)) of these types need nothing special in the
 *  bindings.  An argument that is not a member of the right class, such as an
 *  int or a member of another enum, does not convert, and pybind11 raises
 *  TypeError. */

#include <pybind11/eval.h>
#include <pybind11/gil_safe_call_once.h>
#include <pybind11/pybind11.h>

#include <cstddef>
#include <type_traits>
#include <vector>

#include "dex.hpp"

namespace dexpp_py {

namespace py = pybind11;

/** Define the enum classes in module @p m.  The values are those of the C++
 *  enums; the static_asserts after this function check them. */
inline void define_enums(py::module_ &m)
{
    py::exec(R"py(
import enum as _enum


class InvokeKind(_enum.Enum):
    """The kind of invoke instruction at a call site."""

    VIRTUAL = 0
    SUPER = 1
    DIRECT = 2
    STATIC = 3
    INTERFACE = 4
    POLYMORPHIC = 5
    CUSTOM = 6


class EdgeOrigin(_enum.Enum):
    """How a call-graph edge came to point at its callee."""

    DECLARED = 0
    INHERITED_RESOLUTION = 1


class CfgEdgeKind(_enum.Enum):
    """The kind of edge between two basic blocks of a control-flow graph."""

    FALLTHROUGH = 0
    GOTO = 1
    BRANCH_TAKEN = 2
    BRANCH_NOT_TAKEN = 3
    SWITCH_CASE = 4
    SWITCH_DEFAULT = 5
    EXCEPTION = 6


class TypeUseKind(_enum.Enum):
    """What an instruction does with the type it names."""

    CONST_CLASS = 0
    CHECK_CAST = 1
    INSTANCE_OF = 2
    NEW_INSTANCE = 3
    NEW_ARRAY = 4
    FILLED_NEW_ARRAY = 5


class OperandWidth(_enum.Enum):
    """How many registers a value takes."""

    SINGLE = 0
    PAIR = 1


class AnnotationVisibility(_enum.Enum):
    """Who can see an annotation."""

    BUILD = 0
    RUNTIME = 1
    SYSTEM = 2


class ComponentKind(_enum.Enum):
    """The kind of a component declared in the manifest."""

    ACTIVITY = 0
    SERVICE = 1
    RECEIVER = 2
    PROVIDER = 3


del _enum
)py",
             m.attr("__dict__"));
}

// The Python members above must have the values of the C++ enumerators with
// the same names, because the conversion goes by value.

static_assert(static_cast<int>(dex::InvokeKind::Virtual) == 0 &&
              static_cast<int>(dex::InvokeKind::Super) == 1 &&
              static_cast<int>(dex::InvokeKind::Direct) == 2 &&
              static_cast<int>(dex::InvokeKind::Static) == 3 &&
              static_cast<int>(dex::InvokeKind::Interface) == 4 &&
              static_cast<int>(dex::InvokeKind::Polymorphic) == 5 &&
              static_cast<int>(dex::InvokeKind::Custom) == 6);
static_assert(static_cast<int>(dex::EdgeOrigin::Declared) == 0 &&
              static_cast<int>(dex::EdgeOrigin::InheritedResolution) == 1);
static_assert(static_cast<int>(dex::CfgEdgeKind::Fallthrough) == 0 &&
              static_cast<int>(dex::CfgEdgeKind::Goto) == 1 &&
              static_cast<int>(dex::CfgEdgeKind::BranchTaken) == 2 &&
              static_cast<int>(dex::CfgEdgeKind::BranchNotTaken) == 3 &&
              static_cast<int>(dex::CfgEdgeKind::SwitchCase) == 4 &&
              static_cast<int>(dex::CfgEdgeKind::SwitchDefault) == 5 &&
              static_cast<int>(dex::CfgEdgeKind::Exception) == 6);
static_assert(static_cast<int>(dex::TypeUseKind::ConstClass) == 0 &&
              static_cast<int>(dex::TypeUseKind::CheckCast) == 1 &&
              static_cast<int>(dex::TypeUseKind::InstanceOf) == 2 &&
              static_cast<int>(dex::TypeUseKind::NewInstance) == 3 &&
              static_cast<int>(dex::TypeUseKind::NewArray) == 4 &&
              static_cast<int>(dex::TypeUseKind::FilledNewArray) == 5);
static_assert(static_cast<int>(dex::OperandWidth::Single) == 0 &&
              static_cast<int>(dex::OperandWidth::Pair) == 1);
static_assert(static_cast<int>(dex::AnnotationVisibility::Build) == 0 &&
              static_cast<int>(dex::AnnotationVisibility::Runtime) == 1 &&
              static_cast<int>(dex::AnnotationVisibility::System) == 2);
static_assert(static_cast<int>(dex::apk::Component::Kind::Activity) == 0 &&
              static_cast<int>(dex::apk::Component::Kind::Service) == 1 &&
              static_cast<int>(dex::apk::Component::Kind::Receiver) == 2 &&
              static_cast<int>(dex::apk::Component::Kind::Provider) == 3);

/** The Python name of each C++ enum: `name` for the lookup in the module,
 *  `signature` for the function signatures pybind11 writes in docstrings. */
template <typename E> struct EnumTraits;

#define DEXPP_PY_ENUM(cpp_type, py_name)                                                           \
    template <> struct EnumTraits<cpp_type>                                                        \
    {                                                                                              \
        static constexpr const char *name = py_name;                                               \
        static constexpr auto signature = py::detail::const_name("dexpp." py_name);                \
    }

DEXPP_PY_ENUM(dex::InvokeKind, "InvokeKind");
DEXPP_PY_ENUM(dex::EdgeOrigin, "EdgeOrigin");
DEXPP_PY_ENUM(dex::CfgEdgeKind, "CfgEdgeKind");
DEXPP_PY_ENUM(dex::TypeUseKind, "TypeUseKind");
DEXPP_PY_ENUM(dex::OperandWidth, "OperandWidth");
DEXPP_PY_ENUM(dex::AnnotationVisibility, "AnnotationVisibility");
DEXPP_PY_ENUM(dex::apk::Component::Kind, "ComponentKind");

#undef DEXPP_PY_ENUM

/** One Python enum class and its members, indexed by value. */
struct EnumClass
{
    py::object cls;
    std::vector<py::object> members; ///< members[v] has value v; empty slot for a gap
};

/** The Python class of enum @p E, looked up in the module once.  The members
 *  are kept by value, so converting a C++ value is an index, not a call into
 *  Python's enum machinery: code that reads `edge.kind` on millions of edges
 *  stays fast. */
template <typename E> const EnumClass &enum_class()
{
    PYBIND11_CONSTINIT static py::gil_safe_call_once_and_store<EnumClass> storage;
    return storage
        .call_once_and_store_result([] {
            EnumClass result{py::module_::import("dexpp").attr(EnumTraits<E>::name), {}};
            for (py::handle member : result.cls) {
                auto value = member.attr("value").cast<std::size_t>();
                if (value >= result.members.size())
                    result.members.resize(value + 1);
                result.members[value] = py::reinterpret_borrow<py::object>(member);
            }
            return result;
        })
        .get_stored();
}

/** The type_caster of every enum above, by value in both directions. */
template <typename E> class EnumCaster
{
  public:
    PYBIND11_TYPE_CASTER(E, EnumTraits<E>::signature);

    /** Python to C++: only a member of E's Python class converts. */
    bool load(py::handle src, bool /*convert*/)
    {
        if (!py::isinstance(src, enum_class<E>().cls))
            return false;
        value = static_cast<E>(src.attr("value").cast<std::underlying_type_t<E>>());
        return true;
    }

    /** C++ to Python: the member with the same value.  A value the Python
     *  class does not have (the C++ enum grew and the Python class did not)
     *  raises ValueError from the enum's own lookup. */
    static py::handle cast(E src, py::return_value_policy /*policy*/, py::handle /*parent*/)
    {
        const EnumClass &e = enum_class<E>();
        auto index = static_cast<std::size_t>(src);
        if (index < e.members.size() && e.members[index])
            return e.members[index].inc_ref();
        return e.cls(static_cast<std::underlying_type_t<E>>(src)).release();
    }
};

} // namespace dexpp_py

namespace pybind11::detail {

template <> class type_caster<dex::InvokeKind> : public dexpp_py::EnumCaster<dex::InvokeKind>
{
};
template <> class type_caster<dex::EdgeOrigin> : public dexpp_py::EnumCaster<dex::EdgeOrigin>
{
};
template <> class type_caster<dex::CfgEdgeKind> : public dexpp_py::EnumCaster<dex::CfgEdgeKind>
{
};
template <> class type_caster<dex::TypeUseKind> : public dexpp_py::EnumCaster<dex::TypeUseKind>
{
};
template <> class type_caster<dex::OperandWidth> : public dexpp_py::EnumCaster<dex::OperandWidth>
{
};
template <>
class type_caster<dex::AnnotationVisibility>
    : public dexpp_py::EnumCaster<dex::AnnotationVisibility>
{
};
template <>
class type_caster<dex::apk::Component::Kind>
    : public dexpp_py::EnumCaster<dex::apk::Component::Kind>
{
};

} // namespace pybind11::detail
