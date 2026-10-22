/** Python bindings for dex::model, dexpp's editable DEX model.
 *
 *  Registered into the single `dexpp` module by register_model(), called from
 *  dexpp_module.cpp.  Same conventions as the analysis binding:
 *  - read/write errors raise FileNotFoundError (a missing file) or ValueError.
 *  - a not-found class is None (class_info) or False (has_class).
 *  - access flags are a plain int bitmask; DEX bytes are Python `bytes`.
 *
 *  Scope matches the C / Go model binding: whole classes cross the boundary
 *  (read, inspect, move/copy/remove, rename, validate, write).  Building a
 *  class instruction by instruction stays in the C++ API. */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "model/model.hpp"

namespace py = pybind11;

namespace {

using dex::model::Dex;

/** Raise the right Python exception for a model Error: FileNotFoundError when
 *  a file could not be opened, ValueError otherwise (malformed input, an
 *  unsupported feature, an invalid model, a pool overflow, a version too low).
 *  For Invalid, the message already summarizes validate()'s problems. */
[[noreturn]] void raise(const dex::model::Error &error)
{
    if (error.kind == dex::model::Error::Kind::Io) {
        PyErr_SetString(PyExc_FileNotFoundError, error.message.c_str());
        throw py::error_already_set();
    }
    throw py::value_error(error.message);
}

/** The index of the class with this descriptor, or -1. */
long class_index(const Dex &dex, std::string_view descriptor)
{
    for (std::size_t i = 0; i < dex.classes.size(); ++i)
        if (dex.classes[i].type == descriptor)
            return static_cast<long>(i);
    return -1;
}

/** Facts about one class, as a Python object. */
struct ClassInfo
{
    std::string descriptor;
    uint32_t access_flags;
    std::optional<std::string> superclass;
    std::optional<std::string> source_file;
    std::size_t interface_count;
    std::size_t field_count;
    std::size_t method_count;
};

} // namespace

void register_model(py::module_ &m)
{
    py::class_<dex::model::Problem>(m, "Problem",
                                    "One rule of the DEX format a model breaks, and where.")
        .def_readonly("where", &dex::model::Problem::where,
                      "Where the problem is, e.g. \"Lcom/a/B;->run()V, item 12\"; empty for the "
                      "whole file.")
        .def_readonly("message", &dex::model::Problem::message, "What is wrong.")
        .def("__repr__", [](const dex::model::Problem &p) {
            return "Problem(where=" + py::repr(py::cast(p.where)).cast<std::string>() +
                   ", message=" + py::repr(py::cast(p.message)).cast<std::string>() + ")";
        });

    py::class_<dex::model::PoolStats>(m, "PoolStats",
                                      "How many entries each pool of the written DEX file would "
                                      "have.  The format indexes types and protos with 16 bits "
                                      "(<= 65535) and fields and methods with 16 bits (<= 65536).")
        .def_readonly("strings", &dex::model::PoolStats::strings)
        .def_readonly("types", &dex::model::PoolStats::types)
        .def_readonly("protos", &dex::model::PoolStats::protos)
        .def_readonly("fields", &dex::model::PoolStats::fields)
        .def_readonly("methods", &dex::model::PoolStats::methods)
        .def_readonly("method_handles", &dex::model::PoolStats::method_handles)
        .def_readonly("call_sites", &dex::model::PoolStats::call_sites)
        .def("__repr__", [](const dex::model::PoolStats &s) {
            return "PoolStats(strings=" + std::to_string(s.strings) +
                   ", types=" + std::to_string(s.types) + ", protos=" + std::to_string(s.protos) +
                   ", fields=" + std::to_string(s.fields) +
                   ", methods=" + std::to_string(s.methods) +
                   ", method_handles=" + std::to_string(s.method_handles) +
                   ", call_sites=" + std::to_string(s.call_sites) + ")";
        });

    py::class_<ClassInfo>(m, "ClassInfo", "Facts about one class in a Model.")
        .def_readonly("descriptor", &ClassInfo::descriptor)
        .def_readonly("access_flags", &ClassInfo::access_flags, "DEX access-flag bitmask.")
        .def_readonly("superclass", &ClassInfo::superclass,
                      "Superclass descriptor, or None for java.lang.Object.")
        .def_readonly("source_file", &ClassInfo::source_file, "Source file name, or None.")
        .def_readonly("interface_count", &ClassInfo::interface_count)
        .def_readonly("field_count", &ClassInfo::field_count)
        .def_readonly("method_count", &ClassInfo::method_count)
        .def("__repr__", [](const ClassInfo &c) { return "ClassInfo(" + c.descriptor + ")"; });

    py::class_<Dex>(
        m, "Model",
        "An editable in-memory DEX file (dex::model).  Read a DEX file into a Model, change it at "
        "the granularity of whole classes, and write a DEX file again.  Editing inside a method is "
        "not exposed here; building a class instruction by instruction stays in the C++ API.")
        // ----- construction -----
        .def(py::init([](uint32_t version) {
                 Dex dex;
                 dex.version = version;
                 return dex;
             }),
             py::arg("version") = 35,
             "An empty model of the given DEX version (35, 37, 38, 39, 40).")
        .def_static(
            "read",
            [](py::buffer data, bool debug_info) {
                py::buffer_info info = data.request();
                dex::model::ReadOptions options;
                options.debug_info = debug_info;
                auto bytes = std::span<const uint8_t>(static_cast<const uint8_t *>(info.ptr),
                                                      static_cast<std::size_t>(info.size) *
                                                          static_cast<std::size_t>(info.itemsize));
                auto result = dex::model::read(bytes, options);
                if (!result)
                    raise(result.error());
                return std::move(*result);
            },
            py::arg("data"), py::arg("debug_info") = true,
            "Read DEX bytes into a model.  debug_info keeps line numbers and local variables.")
        .def_static(
            "read_file",
            [](const std::string &path, bool debug_info) {
                dex::model::ReadOptions options;
                options.debug_info = debug_info;
                auto result = dex::model::read_file(path, options);
                if (!result)
                    raise(result.error());
                return std::move(*result);
            },
            py::arg("path"), py::arg("debug_info") = true, "Read a DEX file into a model.")
        .def(
            "clone", [](const Dex &dex) { return dex; }, "A deep copy of the model.")
        // ----- version, pools, validation -----
        .def_property(
            "version", [](const Dex &dex) { return dex.version; },
            [](Dex &dex, uint32_t version) { dex.version = version; },
            "The DEX format version write() will use.")
        .def_property_readonly(
            "required_version", [](const Dex &dex) { return dex::model::required_version(dex); },
            "The lowest DEX version with every feature the model uses.")
        .def_property_readonly(
            "pool_stats", [](const Dex &dex) { return dex::model::pool_stats(dex); },
            "The pool sizes the model would write (see PoolStats).")
        .def(
            "validate", [](const Dex &dex) { return dex::model::validate(dex); },
            "Every rule of the format the model breaks, as a list of Problem; empty if write() can "
            "encode it.")
        // ----- classes -----
        .def("__len__", [](const Dex &dex) { return dex.classes.size(); })
        .def_property_readonly(
            "class_count", [](const Dex &dex) { return dex.classes.size(); },
            "The number of classes in the model.")
        .def_property_readonly(
            "class_descriptors",
            [](const Dex &dex) {
                std::vector<std::string> out;
                out.reserve(dex.classes.size());
                for (const auto &cls : dex.classes)
                    out.push_back(cls.type);
                return out;
            },
            "Every class's descriptor, in model order.")
        .def(
            "has_class",
            [](const Dex &dex, std::string_view descriptor) {
                return class_index(dex, descriptor) >= 0;
            },
            py::arg("descriptor"), "Whether the model has a class with this descriptor.")
        .def(
            "__contains__",
            [](const Dex &dex, std::string_view descriptor) {
                return class_index(dex, descriptor) >= 0;
            },
            py::arg("descriptor"))
        .def(
            "class_info",
            [](const Dex &dex, std::string_view descriptor) -> py::object {
                long i = class_index(dex, descriptor);
                if (i < 0)
                    return py::none();
                const dex::model::ClassDef &cls = dex.classes[static_cast<std::size_t>(i)];
                return py::cast(ClassInfo{cls.type, static_cast<uint32_t>(cls.access),
                                          cls.superclass, cls.source_file, cls.interfaces.size(),
                                          cls.fields.size(), cls.methods.size()});
            },
            py::arg("descriptor"),
            "Facts about the class with this descriptor (a ClassInfo), or None if absent.")
        .def(
            "class_interfaces",
            [](const Dex &dex, std::string_view descriptor) {
                long i = class_index(dex, descriptor);
                if (i < 0)
                    return std::vector<std::string>{};
                return dex.classes[static_cast<std::size_t>(i)].interfaces;
            },
            py::arg("descriptor"), "The interface descriptors the class implements.")
        // ----- editing by whole class -----
        .def(
            "move_class",
            [](Dex &dst, Dex &src, std::string_view descriptor) {
                if (class_index(dst, descriptor) >= 0)
                    throw py::value_error(std::string("destination already has class ") +
                                          std::string(descriptor));
                long i = class_index(src, descriptor);
                if (i < 0)
                    throw py::value_error(std::string("source has no class ") +
                                          std::string(descriptor));
                auto it = src.classes.begin() + i;
                dst.classes.push_back(std::move(*it));
                src.classes.erase(it);
            },
            py::arg("src"), py::arg("descriptor"),
            "Move the class out of src and into this model.  The class is moved, not copied.  "
            "Raises ValueError if src has no such class or this model already has one.")
        .def(
            "copy_class",
            [](Dex &dst, const Dex &src, std::string_view descriptor) {
                if (class_index(dst, descriptor) >= 0)
                    throw py::value_error(std::string("destination already has class ") +
                                          std::string(descriptor));
                long i = class_index(src, descriptor);
                if (i < 0)
                    throw py::value_error(std::string("source has no class ") +
                                          std::string(descriptor));
                dst.classes.push_back(src.classes[static_cast<std::size_t>(i)]);
            },
            py::arg("src"), py::arg("descriptor"),
            "Copy the class from src into this model, leaving src unchanged.  Raises ValueError as "
            "move_class does.")
        .def(
            "remove_class",
            [](Dex &dex, std::string_view descriptor) {
                long i = class_index(dex, descriptor);
                if (i < 0)
                    return false;
                dex.classes.erase(dex.classes.begin() + i);
                return true;
            },
            py::arg("descriptor"),
            "Remove the class with this descriptor, returning whether one was removed.")
        .def(
            "rename_types",
            [](Dex &dex, const std::unordered_map<std::string, std::string> &mapping) {
                dex::model::rename_types(dex,
                                         [&](std::string_view type) -> std::optional<std::string> {
                                             auto it = mapping.find(std::string(type));
                                             if (it == mapping.end())
                                                 return std::nullopt;
                                             return it->second;
                                         });
            },
            py::arg("mapping"),
            "Rename every reference to each class in the mapping (old descriptor -> new).  A "
            "descriptor not in the mapping is unchanged; string constants are never renamed.")
        // ----- writing -----
        .def(
            "write",
            [](const Dex &dex, bool debug_info) {
                dex::model::WriteOptions options;
                options.debug_info = debug_info;
                auto result = dex::model::write(dex, options);
                if (!result)
                    raise(result.error());
                return py::bytes(reinterpret_cast<const char *>(result->data()), result->size());
            },
            py::arg("debug_info") = true,
            "The model as DEX bytes.  Raises ValueError if the model is invalid, a pool overflows, "
            "or a feature needs a higher version.")
        .def(
            "write_file",
            [](const Dex &dex, const std::string &path, bool debug_info) {
                dex::model::WriteOptions options;
                options.debug_info = debug_info;
                auto result = dex::model::write_file(dex, path, options);
                if (!result)
                    raise(result.error());
            },
            py::arg("path"), py::arg("debug_info") = true, "Write the model to a DEX file.")
        .def("__repr__", [](const Dex &dex) {
            return "Model(version=" + std::to_string(dex.version) +
                   ", classes=" + std::to_string(dex.classes.size()) + ")";
        });
}
