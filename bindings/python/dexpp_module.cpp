/** Python bindings for the dex++ analysis API.
 *
 *  Mirrors the public surface of src/dex.hpp.  Conventions at the boundary:
 *  - std::expected errors raise FileNotFoundError / ValueError.
 *  - Null handles (is_valid() == false) and not-found sentinels become None.
 *  - TypeDescriptor is flattened to a plain str: unlike the other handles it
 *    does not keep the AnalysisContext alive, so exposing it as an object
 *    would let Python code dangle it past the context's lifetime.
 *  - Small span-returning accessors copy into Python lists.  The whole-program
 *    index tables (CallGraph.nodes/edges, Xrefs.referrers, Cfg.blocks) are
 *    zero-copy sequence views instead, and call-graph nodes/edges are handles
 *    with direct navigation (edge.callee, node.out_edges) — see bind_view.
 *  - Lazily-built singletons (call_graph / class_hierarchy / xrefs / cfg) are
 *    returned by reference with keep_alive so the owning context outlives them. */

#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dex.hpp"

namespace py = pybind11;
using namespace dex;
using namespace dex::apk;

namespace {

/** Carrier for AnalysisError::Code::FileNotFound; translated to Python's
 *  FileNotFoundError by the translator registered in the module init. */
struct DexFileNotFound : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Convert a possibly-null handle to a Python object (None when invalid). */
py::object class_or_none(Class cls)
{
    if (!cls.is_valid())
        return py::none();
    return py::cast(std::move(cls));
}

std::string method_repr_id(const MethodId &id)
{
    return id.class_descriptor + "->" + id.name + id.proto;
}

/** Recursively convert a resolved AnnotationValue into native Python objects:
 *  None / bool / int / float / str / list, the bound *Ref classes for typed
 *  pool references, and Annotation for nested annotations. */
py::object annotation_value_to_py(const AnnotationValue &v)
{
    return std::visit(
        [](const auto &x) -> py::object {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return py::none();
            else if constexpr (std::is_same_v<T, std::vector<AnnotationValue>>) {
                py::list out;
                for (const auto &item : x)
                    out.append(annotation_value_to_py(item));
                return out;
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<const Annotation>>)
                return py::cast(Annotation(*x)); // copy into a Python-owned object
            else
                return py::cast(x);
        },
        v.value);
}

// ===== zero-copy views and call-graph handles =====
//
// The whole-program structures (call graph, xrefs, CFG) are flat C++ tables
// that refer to each other by index: an edge names its callee as an index into
// the node table, an xref site names its method as an index into referrers.
// Copying a table into a Python list on every property access made the
// natural `cg.nodes[e.callee_node]` cost a full copy per edge.  Instead each
// table is exposed as a read-only sequence view over the C++ storage: O(1)
// indexing, elements built only when touched, nothing copied up front.  A view
// holds the Python object that owns the table, which keeps the
// AnalysisContext alive for as long as the view or its elements live.

std::size_t checked_index(py::ssize_t i, std::size_t size)
{
    if (i < 0)
        i += static_cast<py::ssize_t>(size);
    if (i < 0 || static_cast<std::size_t>(i) >= size)
        throw py::index_error("index out of range");
    return static_cast<std::size_t>(i);
}

/** Forward iterator over a view, yielding view.at(pos). */
template <typename View> struct ViewIterator
{
    const View *view;
    std::size_t pos;

    py::object operator*() const { return view->at(pos); }
    ViewIterator &operator++()
    {
        ++pos;
        return *this;
    }
    bool operator==(const ViewIterator &other) const { return pos == other.pos; }
};

/** Bind @p View (which provides `size()` and `py::object at(i)`) as a
 *  read-only Python sequence: len, int and slice indexing, iteration. */
template <typename View>
py::class_<View> bind_view(py::module_ &m, const char *name, const char *doc)
{
    py::class_<View> cls(m, name, doc);
    cls.def("__len__", &View::size)
        .def("__getitem__",
             [](const View &v, py::ssize_t i) { return v.at(checked_index(i, v.size())); })
        .def("__getitem__",
             [](const View &v, const py::slice &s) {
                 std::size_t start = 0, stop = 0, step = 0, length = 0;
                 if (!s.compute(v.size(), &start, &stop, &step, &length))
                     throw py::error_already_set();
                 py::list out;
                 for (std::size_t k = 0; k < length; ++k, start += step)
                     out.append(v.at(start));
                 return out;
             })
        .def(
            "__iter__",
            [](const View &v) {
                return py::make_iterator(ViewIterator<View>{&v, 0},
                                         ViewIterator<View>{&v, v.size()});
            },
            py::keep_alive<0, 1>());
    py::module_::import("collections.abc").attr("Sequence").attr("register")(cls);
    return cls;
}

/** Handle to one call-graph node, bound as `CallNode`.  Like Class/Method it
 *  is a cheap reference, not a copy: it holds the Python CallGraph object
 *  (keeping the context alive) and the node's index. */
struct NodeRef
{
    py::object owner;
    const CallGraph *graph;
    uint32_t index;

    const CallNode &get() const { return graph->nodes()[index]; }
    bool operator==(const NodeRef &other) const
    {
        return graph == other.graph && index == other.index;
    }
};

/** Handle to one call-graph edge, bound as `CallEdge`. */
struct EdgeRef
{
    py::object owner;
    const CallGraph *graph;
    uint32_t index;

    const CallEdge &get() const { return graph->edges()[index]; }
    bool operator==(const EdgeRef &other) const
    {
        return graph == other.graph && index == other.index;
    }
};

struct CallNodeView
{
    py::object owner;
    const CallGraph *graph;

    std::size_t size() const { return graph->nodes().size(); }
    py::object at(std::size_t i) const
    {
        return py::cast(NodeRef{owner, graph, static_cast<uint32_t>(i)});
    }
};

/** All edges of the graph (when @p subset is null), or the edges whose
 *  indices @p subset lists — a node's outgoing / incoming vector, or the
 *  result of where().  @p storage owns @p subset when where() built it. */
struct CallEdgeView
{
    py::object owner;
    const CallGraph *graph;
    const std::vector<uint32_t> *subset = nullptr;
    std::shared_ptr<const std::vector<uint32_t>> storage;

    std::size_t size() const { return subset ? subset->size() : graph->edges().size(); }
    uint32_t index_at(std::size_t i) const
    {
        return subset ? (*subset)[i] : static_cast<uint32_t>(i);
    }
    py::object at(std::size_t i) const { return py::cast(EdgeRef{owner, graph, index_at(i)}); }

    /** The edges of this view matching every given filter, selected in C++:
     *  CHA fan-out can make the graph tens of millions of edges, far too many
     *  to filter one by one in Python. */
    CallEdgeView where(std::optional<EdgeOrigin> origin, std::optional<InvokeKind> kind) const
    {
        auto picked = std::make_shared<std::vector<uint32_t>>();
        auto edges = graph->edges();
        for (std::size_t i = 0, n = size(); i < n; ++i) {
            uint32_t index = index_at(i);
            const CallEdge &e = edges[index];
            if ((!origin || e.origin == *origin) && (!kind || e.kind == *kind))
                picked->push_back(index);
        }
        return CallEdgeView{owner, graph, picked.get(), picked};
    }
};

struct ReferrerView
{
    py::object owner;
    const Xrefs *xrefs;

    std::size_t size() const { return xrefs->referrers().size(); }
    py::object at(std::size_t i) const { return py::cast(xrefs->referrers()[i]); }
};

struct BasicBlockView
{
    py::object owner;
    const Cfg *cfg;

    std::size_t size() const { return cfg->blocks().size(); }
    py::object at(std::size_t i) const { return py::cast(cfg->blocks()[i]); }
};

py::object method_or_none(std::optional<Method> m)
{
    if (!m.has_value())
        return py::none();
    return py::cast(std::move(*m));
}

} // namespace

PYBIND11_MODULE(dexpp, m)
{
    m.doc() = "Python bindings for dex++, a static-analysis library for Android DEX files";

    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p)
                std::rethrow_exception(p);
        }
        catch (const DexFileNotFound &e) {
            PyErr_SetString(PyExc_FileNotFoundError, e.what());
        }
    });

    // ===== free functions =====

    m.def("opcode_name", &opcode_name, py::arg("opcode"),
          "Mnemonic for a raw Dalvik opcode byte (e.g. 0x6e -> 'invoke-virtual').");
    m.def("to_string", &to_string, py::arg("instruction"),
          "Render a decoded instruction as '<mnemonic> <operands>' with raw pool indices.");

    // ===== enums =====

    py::enum_<InvokeKind>(m, "InvokeKind")
        .value("VIRTUAL", InvokeKind::Virtual)
        .value("SUPER", InvokeKind::Super)
        .value("DIRECT", InvokeKind::Direct)
        .value("STATIC", InvokeKind::Static)
        .value("INTERFACE", InvokeKind::Interface)
        .value("POLYMORPHIC", InvokeKind::Polymorphic)
        .value("CUSTOM", InvokeKind::Custom);

    py::enum_<EdgeOrigin>(m, "EdgeOrigin")
        .value("DECLARED", EdgeOrigin::Declared)
        .value("CHA_OVERRIDE", EdgeOrigin::ChaOverride)
        .value("INHERITED_RESOLUTION", EdgeOrigin::InheritedResolution);

    py::enum_<EdgeKind>(m, "EdgeKind")
        .value("FALLTHROUGH", EdgeKind::Fallthrough)
        .value("GOTO", EdgeKind::Goto)
        .value("BRANCH_TAKEN", EdgeKind::BranchTaken)
        .value("BRANCH_NOT_TAKEN", EdgeKind::BranchNotTaken)
        .value("SWITCH_CASE", EdgeKind::SwitchCase)
        .value("SWITCH_DEFAULT", EdgeKind::SwitchDefault)
        .value("EXCEPTION", EdgeKind::Exception);

    py::enum_<TypeRefKind>(m, "TypeRefKind")
        .value("CONST_CLASS", TypeRefKind::ConstClass)
        .value("CHECK_CAST", TypeRefKind::CheckCast)
        .value("INSTANCE_OF", TypeRefKind::InstanceOf)
        .value("NEW_INSTANCE", TypeRefKind::NewInstance)
        .value("NEW_ARRAY", TypeRefKind::NewArray)
        .value("FILLED_NEW_ARRAY", TypeRefKind::FilledNewArray);

    py::enum_<OperandWidth>(m, "OperandWidth")
        .value("SINGLE", OperandWidth::Single)
        .value("PAIR", OperandWidth::Pair);

    py::enum_<AnnotationVisibility>(m, "AnnotationVisibility")
        .value("BUILD", AnnotationVisibility::Build)
        .value("RUNTIME", AnnotationVisibility::Runtime)
        .value("SYSTEM", AnnotationVisibility::System);

    // ===== identities =====

    py::class_<MethodId>(m, "MethodId")
        .def(py::init<std::string, std::string, std::string>(), py::arg("class_descriptor"),
             py::arg("name"), py::arg("proto"))
        .def_readonly("class_descriptor", &MethodId::class_descriptor)
        .def_readonly("name", &MethodId::name)
        .def_readonly("proto", &MethodId::proto)
        .def(py::self == py::self)
        .def("__hash__", [](const MethodId &id) { return MethodIdHash{}(id); })
        .def("__repr__", [](const MethodId &id) { return "MethodId(" + method_repr_id(id) + ")"; });

    py::class_<FieldId>(m, "FieldId")
        .def(py::init<std::string, std::string, std::string>(), py::arg("class_descriptor"),
             py::arg("name"), py::arg("type"))
        .def_readonly("class_descriptor", &FieldId::class_descriptor)
        .def_readonly("name", &FieldId::name)
        .def_readonly("type", &FieldId::type)
        .def(py::self == py::self)
        .def("__hash__", [](const FieldId &id) { return FieldIdHash{}(id); })
        .def("__repr__", [](const FieldId &id) {
            return "FieldId(" + id.class_descriptor + "->" + id.name + ":" + id.type + ")";
        });

    // ===== annotations =====

    py::class_<AnnotationValue::TypeRef>(m, "TypeRef")
        .def_readonly("descriptor", &AnnotationValue::TypeRef::descriptor)
        .def(py::self == py::self)
        .def("__repr__",
             [](const AnnotationValue::TypeRef &t) { return "TypeRef(" + t.descriptor + ")"; });

    py::class_<AnnotationValue::EnumRef>(m, "EnumRef")
        .def_readonly("constant", &AnnotationValue::EnumRef::constant)
        .def(py::self == py::self)
        .def("__repr__", [](const AnnotationValue::EnumRef &e) {
            return "EnumRef(" + e.constant.class_descriptor + "->" + e.constant.name + ")";
        });

    py::class_<AnnotationValue::FieldRef>(m, "FieldRef")
        .def_readonly("field", &AnnotationValue::FieldRef::field)
        .def(py::self == py::self);

    py::class_<AnnotationValue::MethodRef>(m, "MethodRef")
        .def_readonly("method", &AnnotationValue::MethodRef::method)
        .def(py::self == py::self);

    py::class_<AnnotationValue::MethodTypeRef>(m, "MethodTypeRef")
        .def_readonly("proto", &AnnotationValue::MethodTypeRef::proto)
        .def(py::self == py::self);

    py::class_<AnnotationValue::MethodHandleRef>(m, "MethodHandleRef")
        .def_readonly("index", &AnnotationValue::MethodHandleRef::index)
        .def(py::self == py::self);

    py::class_<Annotation>(m, "Annotation")
        .def_readonly("type_descriptor", &Annotation::type_descriptor)
        .def_readonly("visibility", &Annotation::visibility)
        .def_property_readonly("elements",
                               [](const Annotation &a) {
                                   py::dict out;
                                   for (const auto &e : a.elements)
                                       out[py::str(e.name)] = annotation_value_to_py(e.value);
                                   return out;
                               })
        .def("__repr__",
             [](const Annotation &a) { return "Annotation(" + a.type_descriptor + ")"; });

    // ===== instructions =====

    py::class_<InstructionBase>(m, "InstructionBase")
        .def_readonly("offset", &InstructionBase::offset)
        .def_readonly("opcode", &InstructionBase::opcode)
        .def_readonly("size", &InstructionBase::size);

    py::class_<NopInstruction>(m, "NopInstruction").def_readonly("base", &NopInstruction::base);

    py::class_<GotoInstruction>(m, "GotoInstruction")
        .def_readonly("base", &GotoInstruction::base)
        .def_readonly("branch_offset", &GotoInstruction::branch_offset);

    py::class_<IfInstruction>(m, "IfInstruction")
        .def_readonly("base", &IfInstruction::base)
        .def_readonly("branch_offset", &IfInstruction::branch_offset)
        .def_readonly("reg_a", &IfInstruction::reg_a)
        .def_readonly("reg_b", &IfInstruction::reg_b);

    py::class_<IfZeroInstruction>(m, "IfZeroInstruction")
        .def_readonly("base", &IfZeroInstruction::base)
        .def_readonly("branch_offset", &IfZeroInstruction::branch_offset)
        .def_readonly("reg", &IfZeroInstruction::reg);

    py::class_<SwitchCase>(m, "SwitchCase")
        .def_readonly("key", &SwitchCase::key)
        .def_readonly("target", &SwitchCase::target);

    py::class_<SwitchInstruction>(m, "SwitchInstruction")
        .def_readonly("base", &SwitchInstruction::base)
        .def_readonly("payload_offset", &SwitchInstruction::payload_offset)
        .def_readonly("reg", &SwitchInstruction::reg)
        .def_readonly("is_packed", &SwitchInstruction::is_packed)
        .def_readonly("cases", &SwitchInstruction::cases);

    py::class_<ReturnInstruction>(m, "ReturnInstruction")
        .def_readonly("base", &ReturnInstruction::base)
        .def_readonly("reg", &ReturnInstruction::reg)
        .def_readonly("width", &ReturnInstruction::width);

    py::class_<ThrowInstruction>(m, "ThrowInstruction")
        .def_readonly("base", &ThrowInstruction::base)
        .def_readonly("reg", &ThrowInstruction::reg);

    py::class_<MoveInstruction>(m, "MoveInstruction")
        .def_readonly("base", &MoveInstruction::base)
        .def_readonly("dest", &MoveInstruction::dest)
        .def_readonly("src", &MoveInstruction::src)
        .def_readonly("width", &MoveInstruction::width);

    py::class_<ConstInstruction>(m, "ConstInstruction")
        .def_readonly("base", &ConstInstruction::base)
        .def_readonly("dest", &ConstInstruction::dest)
        .def_readonly("value", &ConstInstruction::value)
        .def_readonly("width", &ConstInstruction::width);

    py::class_<ConstStringInstruction>(m, "ConstStringInstruction")
        .def_readonly("base", &ConstStringInstruction::base)
        .def_readonly("dest", &ConstStringInstruction::dest)
        .def_readonly("string_index", &ConstStringInstruction::string_index);

    py::class_<ConstClassInstruction>(m, "ConstClassInstruction")
        .def_readonly("base", &ConstClassInstruction::base)
        .def_readonly("dest", &ConstClassInstruction::dest)
        .def_readonly("type_index", &ConstClassInstruction::type_index);

    py::class_<ConstHandleInstruction>(m, "ConstHandleInstruction")
        .def_readonly("base", &ConstHandleInstruction::base)
        .def_readonly("dest", &ConstHandleInstruction::dest)
        .def_readonly("index", &ConstHandleInstruction::index);

    py::class_<TypeInstruction>(m, "TypeInstruction")
        .def_readonly("base", &TypeInstruction::base)
        .def_readonly("type_index", &TypeInstruction::type_index)
        .def_readonly("dest", &TypeInstruction::dest)
        .def_readonly("src", &TypeInstruction::src);

    py::class_<FilledNewArrayInstruction>(m, "FilledNewArrayInstruction")
        .def_readonly("base", &FilledNewArrayInstruction::base)
        .def_readonly("type_index", &FilledNewArrayInstruction::type_index)
        .def_readonly("arg_regs", &FilledNewArrayInstruction::arg_regs);

    py::class_<MonitorInstruction>(m, "MonitorInstruction")
        .def_readonly("base", &MonitorInstruction::base)
        .def_readonly("reg", &MonitorInstruction::reg)
        .def_readonly("is_enter", &MonitorInstruction::is_enter);

    py::class_<FillArrayInstruction>(m, "FillArrayInstruction")
        .def_readonly("base", &FillArrayInstruction::base)
        .def_readonly("reg", &FillArrayInstruction::reg)
        .def_readonly("payload_offset", &FillArrayInstruction::payload_offset);

    py::class_<CompareInstruction>(m, "CompareInstruction")
        .def_readonly("base", &CompareInstruction::base)
        .def_readonly("dest", &CompareInstruction::dest)
        .def_readonly("src_a", &CompareInstruction::src_a)
        .def_readonly("src_b", &CompareInstruction::src_b)
        .def_readonly("src_width", &CompareInstruction::src_width);

    py::class_<ArrayOpInstruction>(m, "ArrayOpInstruction")
        .def_readonly("base", &ArrayOpInstruction::base)
        .def_readonly("value_reg", &ArrayOpInstruction::value_reg)
        .def_readonly("array_reg", &ArrayOpInstruction::array_reg)
        .def_readonly("index_reg", &ArrayOpInstruction::index_reg)
        .def_readonly("is_write", &ArrayOpInstruction::is_write)
        .def_readonly("width", &ArrayOpInstruction::width);

    py::class_<FieldInstruction>(m, "FieldInstruction")
        .def_readonly("base", &FieldInstruction::base)
        .def_readonly("field_index", &FieldInstruction::field_index)
        .def_readonly("value_reg", &FieldInstruction::value_reg)
        .def_readonly("object_reg", &FieldInstruction::object_reg)
        .def_readonly("is_static", &FieldInstruction::is_static)
        .def_readonly("is_write", &FieldInstruction::is_write)
        .def_readonly("width", &FieldInstruction::width);

    py::class_<InvokeInstruction>(m, "InvokeInstruction")
        .def_readonly("base", &InvokeInstruction::base)
        .def_readonly("method_index", &InvokeInstruction::method_index)
        .def_readonly("arg_regs", &InvokeInstruction::arg_regs);

    py::class_<InvokeCustomInstruction>(m, "InvokeCustomInstruction")
        .def_readonly("base", &InvokeCustomInstruction::base)
        .def_readonly("call_site_index", &InvokeCustomInstruction::call_site_index)
        .def_readonly("arg_regs", &InvokeCustomInstruction::arg_regs);

    py::class_<UnaryOpInstruction>(m, "UnaryOpInstruction")
        .def_readonly("base", &UnaryOpInstruction::base)
        .def_readonly("dest", &UnaryOpInstruction::dest)
        .def_readonly("src", &UnaryOpInstruction::src)
        .def_readonly("dest_width", &UnaryOpInstruction::dest_width)
        .def_readonly("src_width", &UnaryOpInstruction::src_width);

    py::class_<BinaryOpInstruction>(m, "BinaryOpInstruction")
        .def_readonly("base", &BinaryOpInstruction::base)
        .def_readonly("dest", &BinaryOpInstruction::dest)
        .def_readonly("src_a", &BinaryOpInstruction::src_a)
        .def_readonly("src_b", &BinaryOpInstruction::src_b)
        .def_readonly("width", &BinaryOpInstruction::width);

    py::class_<BinaryLitInstruction>(m, "BinaryLitInstruction")
        .def_readonly("base", &BinaryLitInstruction::base)
        .def_readonly("dest", &BinaryLitInstruction::dest)
        .def_readonly("src", &BinaryLitInstruction::src)
        .def_readonly("literal", &BinaryLitInstruction::literal);

    // ===== CFG =====

    py::class_<CfgEdge>(m, "CfgEdge")
        .def_readonly("target_block", &CfgEdge::target_block)
        .def_readonly("kind", &CfgEdge::kind)
        .def_readonly("label", &CfgEdge::label);

    py::class_<BasicBlock>(m, "BasicBlock")
        .def_readonly("index", &BasicBlock::index)
        .def_readonly("start_offset", &BasicBlock::start_offset)
        .def_readonly("end_offset", &BasicBlock::end_offset)
        .def_readonly("first_insn", &BasicBlock::first_insn)
        .def_readonly("insn_count", &BasicBlock::insn_count)
        .def_readonly("successors", &BasicBlock::successors)
        .def_readonly("predecessors", &BasicBlock::predecessors)
        .def_readonly("is_handler_entry", &BasicBlock::is_handler_entry);

    bind_view<BasicBlockView>(m, "BasicBlockView",
                              "Read-only sequence of a CFG's blocks; indexing does not copy "
                              "the others.");

    py::class_<Cfg>(m, "Cfg")
        .def_property_readonly(
            "blocks",
            [](py::object self) { return BasicBlockView{self, &self.cast<const Cfg &>()}; })
        .def_property_readonly("entry",
                               [](const Cfg &c) -> py::object {
                                   if (c.empty())
                                       return py::none();
                                   return py::cast(c.entry());
                               })
        .def("block_at_offset",
             [](const Cfg &c, uint32_t offset) -> py::object {
                 const BasicBlock *b = c.block_at_offset(offset);
                 if (!b)
                     return py::none();
                 return py::cast(*b);
             })
        .def_property_readonly("empty", &Cfg::empty)
        .def("__len__", [](const Cfg &c) { return c.blocks().size(); });

    // ===== handles =====

    py::class_<ClassRef>(m, "ClassRef")
        .def_property_readonly("descriptor",
                               [](const ClassRef &r) { return std::string(r.descriptor()); })
        .def_property_readonly("is_resolved", &ClassRef::is_resolved)
        .def("resolve", [](const ClassRef &r) { return class_or_none(r.resolve()); })
        .def("__repr__",
             [](const ClassRef &r) { return "ClassRef(" + std::string(r.descriptor()) + ")"; });

    py::class_<Field>(m, "Field")
        .def_property_readonly("name", [](const Field &f) { return std::string(f.name()); })
        .def_property_readonly("declaring_class", &Field::declaring_class)
        .def_property_readonly("type",
                               [](const Field &f) { return std::string(f.type().descriptor()); })
        .def_property_readonly(
            "access_flags", [](const Field &f) { return static_cast<uint32_t>(f.access_flags()); })
        .def_property_readonly("is_public", &Field::is_public)
        .def_property_readonly("is_static", &Field::is_static)
        .def_property_readonly("is_final", &Field::is_final)
        .def_property_readonly("annotations", &Field::annotations)
        .def_property_readonly("initial_value",
                               [](const Field &f) -> py::object {
                                   auto v = f.initial_value();
                                   if (!v.has_value())
                                       return py::none();
                                   return annotation_value_to_py(*v);
                               })
        .def("__repr__", [](const Field &f) { return "Field(" + std::string(f.name()) + ")"; });

    // ===== forward references & try blocks =====

    py::class_<MethodCall>(m, "MethodCall")
        .def_readonly("code_offset", &MethodCall::code_offset)
        .def_readonly("target", &MethodCall::target)
        .def_readonly("kind", &MethodCall::kind);

    py::class_<FieldAccess>(m, "FieldAccess")
        .def_readonly("code_offset", &FieldAccess::code_offset)
        .def_readonly("field", &FieldAccess::field)
        .def_readonly("is_write", &FieldAccess::is_write)
        .def_readonly("is_static", &FieldAccess::is_static);

    py::class_<StringLoad>(m, "StringLoad")
        .def_readonly("code_offset", &StringLoad::code_offset)
        .def_property_readonly("value", [](const StringLoad &s) { return std::string(s.value); });

    py::class_<TypeUse>(m, "TypeUse")
        .def_readonly("code_offset", &TypeUse::code_offset)
        .def_property_readonly("descriptor",
                               [](const TypeUse &t) { return std::string(t.descriptor); })
        .def_readonly("kind", &TypeUse::kind);

    py::class_<CatchHandler>(m, "CatchHandler")
        .def_property_readonly("type_descriptor",
                               [](const CatchHandler &h) { return std::string(h.type_descriptor); })
        .def_readonly("handler_offset", &CatchHandler::handler_offset);

    py::class_<TryBlock>(m, "TryBlock")
        .def_readonly("start_offset", &TryBlock::start_offset)
        .def_readonly("end_offset", &TryBlock::end_offset)
        .def_readonly("handlers", &TryBlock::handlers)
        .def_readonly("catch_all_offset", &TryBlock::catch_all_offset);

    py::class_<ParameterRegister>(m, "ParameterRegister")
        .def_readonly("reg", &ParameterRegister::reg)
        .def_property_readonly(
            "type", [](const ParameterRegister &p) { return std::string(p.type.descriptor()); })
        .def_readonly("is_this", &ParameterRegister::is_this)
        .def_readonly("is_wide", &ParameterRegister::is_wide);

    py::class_<Method>(m, "Method")
        .def_property_readonly("name", [](const Method &m_) { return std::string(m_.name()); })
        .def_property_readonly("declaring_class", &Method::declaring_class)
        .def_property_readonly(
            "return_type",
            [](const Method &m_) { return std::string(m_.return_type().descriptor()); })
        .def_property_readonly("parameters",
                               [](const Method &m_) {
                                   std::vector<std::string> out;
                                   for (const auto &p : m_.parameters())
                                       out.emplace_back(p.descriptor());
                                   return out;
                               })
        .def_property_readonly(
            "access_flags",
            [](const Method &m_) { return static_cast<uint32_t>(m_.access_flags()); })
        .def_property_readonly("has_code", &Method::has_code)
        .def_property_readonly("is_constructor", &Method::is_constructor)
        .def_property_readonly("is_static", &Method::is_static)
        .def_property_readonly("is_public", &Method::is_public)
        .def_property_readonly("is_private", &Method::is_private)
        .def_property_readonly("is_abstract", &Method::is_abstract)
        .def_property_readonly("is_native", &Method::is_native)
        .def_property_readonly("instructions",
                               [](const Method &m_) {
                                   auto insns = m_.instructions();
                                   return std::vector<Instruction>(insns.begin(), insns.end());
                               })
        .def_property_readonly("cfg", &Method::cfg, py::return_value_policy::reference_internal)
        .def_property_readonly("register_count", &Method::register_count)
        .def_property_readonly("parameter_registers", &Method::parameter_registers)
        .def_property_readonly("calls", &Method::calls)
        .def_property_readonly("field_accesses", &Method::field_accesses)
        .def_property_readonly("string_loads", &Method::string_loads)
        .def_property_readonly("type_uses", &Method::type_uses)
        .def_property_readonly("try_blocks", &Method::try_blocks)
        .def_property_readonly("annotations", &Method::annotations)
        .def("__repr__", [](const Method &m_) {
            return "Method(" + std::string(m_.declaring_class().descriptor()) + "->" +
                   std::string(m_.name()) + ")";
        });

    py::class_<Class>(m, "Class")
        .def_property_readonly("name", [](const Class &c) { return std::string(c.name()); })
        .def_property_readonly("package", [](const Class &c) { return std::string(c.package()); })
        .def_property_readonly("superclass", &Class::superclass)
        .def_property_readonly("interfaces", &Class::interfaces)
        .def_property_readonly("methods", &Class::methods)
        .def_property_readonly("fields", &Class::fields)
        .def_property_readonly("annotations", &Class::annotations)
        .def_property_readonly(
            "access_flags", [](const Class &c) { return static_cast<uint32_t>(c.access_flags()); })
        .def_property_readonly("source_file",
                               [](const Class &c) -> py::object {
                                   auto sf = c.source_file();
                                   if (!sf.has_value())
                                       return py::none();
                                   return py::cast(std::string(*sf));
                               })
        .def_property_readonly("is_public", &Class::is_public)
        .def_property_readonly("is_abstract", &Class::is_abstract)
        .def_property_readonly("is_interface", &Class::is_interface)
        .def_property_readonly("is_final", &Class::is_final)
        .def_property_readonly("is_enum", &Class::is_enum)
        .def("__repr__", [](const Class &c) { return "Class(" + std::string(c.name()) + ")"; });

    // ===== call graph =====

    py::class_<EdgeRef>(m, "CallEdge")
        .def_property_readonly("caller_node", [](const EdgeRef &e) { return e.get().caller_node; })
        .def_property_readonly("callee_node", [](const EdgeRef &e) { return e.get().callee_node; })
        .def_property_readonly(
            "caller",
            [](const EdgeRef &e) { return NodeRef{e.owner, e.graph, e.get().caller_node}; },
            "The calling node (same as graph.nodes[caller_node]).")
        .def_property_readonly(
            "callee",
            [](const EdgeRef &e) { return NodeRef{e.owner, e.graph, e.get().callee_node}; },
            "The called node (same as graph.nodes[callee_node]).")
        .def_property_readonly("kind", [](const EdgeRef &e) { return e.get().kind; })
        .def_property_readonly("code_offset", [](const EdgeRef &e) { return e.get().code_offset; })
        .def_property_readonly("origin", [](const EdgeRef &e) { return e.get().origin; })
        .def_property_readonly(
            "via_override",
            [](const EdgeRef &e) {
                PyErr_WarnEx(PyExc_DeprecationWarning,
                             "CallEdge.via_override is deprecated; use CallEdge.origin. "
                             "Note `not via_override` no longer identifies declared edges — "
                             "test origin == EdgeOrigin.DECLARED instead.",
                             1);
                return e.get().origin == EdgeOrigin::ChaOverride;
            },
            "Deprecated: True iff origin == EdgeOrigin.CHA_OVERRIDE.")
        .def(py::self == py::self)
        .def("__hash__", [](const EdgeRef &e) { return std::hash<uint32_t>{}(e.index); })
        .def("__repr__", [](const EdgeRef &e) {
            const auto &nodes = e.graph->nodes();
            return "CallEdge(" + method_repr_id(nodes[e.get().caller_node].id) + " -> " +
                   method_repr_id(nodes[e.get().callee_node].id) + ")";
        });

    py::class_<NodeRef>(m, "CallNode")
        .def_property_readonly("index", [](const NodeRef &n) { return n.index; })
        .def_property_readonly(
            "id", [](const NodeRef &n) -> const MethodId & { return n.get().id; },
            py::return_value_policy::reference_internal)
        .def_property_readonly("resolved", [](const NodeRef &n) { return n.get().resolved; })
        .def_property_readonly("outgoing", [](const NodeRef &n) { return n.get().outgoing; })
        .def_property_readonly("incoming", [](const NodeRef &n) { return n.get().incoming; })
        .def_property_readonly(
            "out_edges",
            [](const NodeRef &n) { return CallEdgeView{n.owner, n.graph, &n.get().outgoing}; },
            "Edges leaving this node (its call sites), as CallEdge objects.")
        .def_property_readonly(
            "in_edges",
            [](const NodeRef &n) { return CallEdgeView{n.owner, n.graph, &n.get().incoming}; },
            "Edges entering this node (its callers' call sites), as CallEdge objects.")
        .def_property_readonly(
            "method", [](const NodeRef &n) { return method_or_none(n.graph->method_for(n.get())); },
            "The Method for a resolved node; None for external methods.")
        .def(py::self == py::self)
        .def("__hash__", [](const NodeRef &n) { return std::hash<uint32_t>{}(n.index); })
        .def("__repr__",
             [](const NodeRef &n) { return "CallNode(" + method_repr_id(n.get().id) + ")"; });

    bind_view<CallNodeView>(m, "CallNodeView",
                            "Read-only sequence of call-graph nodes; indexing does not copy "
                            "the graph.");
    bind_view<CallEdgeView>(m, "CallEdgeView",
                            "Read-only sequence of call-graph edges; indexing does not copy "
                            "the graph.")
        .def("where", &CallEdgeView::where, py::kw_only(), py::arg("origin") = py::none(),
             py::arg("kind") = py::none(),
             "The edges of this view matching every given filter, selected in C++ "
             "(e.g. graph.edges.where(origin=EdgeOrigin.DECLARED) for one edge per "
             "call site, without the class-hierarchy fan-out).");

    py::class_<CallGraph>(m, "CallGraph")
        .def_property_readonly(
            "nodes",
            [](py::object self) { return CallNodeView{self, &self.cast<const CallGraph &>()}; })
        .def_property_readonly(
            "edges",
            [](py::object self) { return CallEdgeView{self, &self.cast<const CallGraph &>()}; })
        .def("find",
             [](py::object self, const MethodId &id) -> py::object {
                 const auto &g = self.cast<const CallGraph &>();
                 const CallNode *n = g.find(id);
                 if (!n)
                     return py::none();
                 return py::cast(NodeRef{self, &g, n->index});
             })
        .def("method_for",
             [](const CallGraph &g, const NodeRef &node) -> py::object {
                 // A node from another context's graph is looked up by id.
                 if (node.graph == &g)
                     return method_or_none(g.method_for(node.get()));
                 const CallNode *own = g.find(node.get().id);
                 if (!own)
                     return py::none();
                 return method_or_none(g.method_for(*own));
             })
        .def_property_readonly("empty", &CallGraph::empty);

    // ===== class hierarchy =====

    py::class_<ClassHierarchy>(m, "ClassHierarchy")
        .def("supertypes",
             [](const ClassHierarchy &h, std::string_view d) {
                 auto v = h.supertypes(d);
                 return std::vector<std::string>(v.begin(), v.end());
             })
        .def("subclasses",
             [](const ClassHierarchy &h, std::string_view d) {
                 auto v = h.subclasses(d);
                 return std::vector<std::string>(v.begin(), v.end());
             })
        .def("implementers",
             [](const ClassHierarchy &h, std::string_view d) {
                 auto v = h.implementers(d);
                 return std::vector<std::string>(v.begin(), v.end());
             })
        .def("all_supertypes",
             [](const ClassHierarchy &h, std::string_view d) {
                 auto v = h.all_supertypes(d);
                 return std::vector<std::string>(v.begin(), v.end());
             })
        .def("all_descendants",
             [](const ClassHierarchy &h, std::string_view d) {
                 auto v = h.all_descendants(d);
                 return std::vector<std::string>(v.begin(), v.end());
             })
        .def("is_subtype_of", &ClassHierarchy::is_subtype_of, py::arg("sub"), py::arg("super"))
        .def("is_loaded", &ClassHierarchy::is_loaded)
        .def("is_interface", &ClassHierarchy::is_interface)
        .def_property_readonly("empty", &ClassHierarchy::empty);

    // ===== xrefs =====

    py::class_<StringXref>(m, "StringXref")
        .def_readonly("referrer", &StringXref::referrer)
        .def_readonly("code_offset", &StringXref::code_offset);

    py::class_<FieldXref>(m, "FieldXref")
        .def_readonly("referrer", &FieldXref::referrer)
        .def_readonly("code_offset", &FieldXref::code_offset)
        .def_readonly("is_write", &FieldXref::is_write)
        .def_readonly("is_static", &FieldXref::is_static);

    py::class_<TypeXref>(m, "TypeXref")
        .def_readonly("referrer", &TypeXref::referrer)
        .def_readonly("code_offset", &TypeXref::code_offset)
        .def_readonly("kind", &TypeXref::kind);

    py::class_<MethodXref>(m, "MethodXref")
        .def_readonly("referrer", &MethodXref::referrer)
        .def_readonly("code_offset", &MethodXref::code_offset)
        .def_readonly("kind", &MethodXref::kind);

    bind_view<ReferrerView>(m, "ReferrerView",
                            "Read-only sequence of xref referrer methods; indexing does not "
                            "copy the others.");

    py::class_<Xrefs>(m, "Xrefs")
        .def_property_readonly(
            "referrers",
            [](py::object self) { return ReferrerView{self, &self.cast<const Xrefs &>()}; })
        .def("referrer_id",
             [](const Xrefs &x, uint32_t referrer) -> MethodId {
                 if (referrer >= x.referrers().size())
                     throw py::index_error("referrer index out of range");
                 return x.referrer_id(referrer);
             })
        .def("referrer_method",
             [](const Xrefs &x, uint32_t referrer) -> py::object {
                 return method_or_none(x.referrer_method(referrer));
             })
        .def("method_refs",
             [](const Xrefs &x, const MethodId &id) {
                 auto v = x.method_refs(id);
                 return std::vector<MethodXref>(v.begin(), v.end());
             })
        .def("method_refs_by_name",
             [](const Xrefs &x, std::string_view name) {
                 std::vector<std::pair<MethodId, std::vector<MethodXref>>> out;
                 for (const auto &[id, sites] : x.method_refs_by_name(name))
                     out.emplace_back(*id, std::vector<MethodXref>(sites.begin(), sites.end()));
                 return out;
             })
        .def("field_refs",
             [](const Xrefs &x, const FieldId &id) {
                 auto v = x.field_refs(id);
                 return std::vector<FieldXref>(v.begin(), v.end());
             })
        .def("field_reads", &Xrefs::field_reads)
        .def("field_writes", &Xrefs::field_writes)
        .def("string_refs",
             [](const Xrefs &x, std::string_view value) {
                 auto v = x.string_refs(value);
                 return std::vector<StringXref>(v.begin(), v.end());
             })
        .def_property_readonly("referenced_strings",
                               [](const Xrefs &x) {
                                   auto v = x.referenced_strings();
                                   return std::vector<std::string>(v.begin(), v.end());
                               })
        .def("type_refs",
             [](const Xrefs &x, std::string_view descriptor) {
                 auto v = x.type_refs(descriptor);
                 return std::vector<TypeXref>(v.begin(), v.end());
             })
        .def_property_readonly("empty", &Xrefs::empty);

    // ===== context =====

    py::class_<AnalysisContext>(m, "AnalysisContext")
        .def_static(
            "from_dex",
            [](const std::string &path) {
                auto result = AnalysisContext::from_dex(path);
                if (!result.has_value()) {
                    if (result.error().code == AnalysisError::Code::FileNotFound)
                        throw DexFileNotFound(result.error().message);
                    throw py::value_error(result.error().message);
                }
                return std::move(result.value());
            },
            py::arg("path"), "Parse a single DEX file from disk.")
        .def_static(
            "from_dex_files",
            [](const std::vector<std::string> &paths) {
                auto result = AnalysisContext::from_dex_files(paths);
                if (!result.has_value()) {
                    if (result.error().code == AnalysisError::Code::FileNotFound)
                        throw DexFileNotFound(result.error().message);
                    throw py::value_error(result.error().message);
                }
                return std::move(result.value());
            },
            py::arg("paths"), "Parse multiple DEX files (e.g. multi-dex splits) in one context.")
        .def_static(
            "from_apk",
            [](const std::string &path, unsigned threads) {
                auto result = AnalysisContext::from_apk(path, {.threads = threads});
                if (!result.has_value()) {
                    if (result.error().code == AnalysisError::Code::FileNotFound)
                        throw DexFileNotFound(result.error().message);
                    throw py::value_error(result.error().message);
                }
                return std::move(result.value());
            },
            py::arg("path"), py::arg("threads") = 0,
            "Open an APK and load every classes*.dex inside it, in multidex order. "
            "threads: 0 = automatic, 1 = load on the calling thread only; extra "
            "threads are only used when at least 3 DEX entries are deflated.")
        .def_property_readonly("classes", &AnalysisContext::classes)
        .def_property_readonly("strings",
                               [](const AnalysisContext &ctx) {
                                   auto v = ctx.strings();
                                   return std::vector<std::string>(v.begin(), v.end());
                               })
        .def("find_class",
             [](const AnalysisContext &ctx, std::string_view descriptor) -> py::object {
                 auto cls = ctx.find_class(descriptor);
                 if (!cls.has_value())
                     return py::none();
                 return py::cast(std::move(*cls));
             })
        .def_property_readonly("call_graph", &AnalysisContext::call_graph,
                               py::return_value_policy::reference_internal)
        .def_property_readonly("class_hierarchy", &AnalysisContext::class_hierarchy,
                               py::return_value_policy::reference_internal)
        .def_property_readonly("xrefs", &AnalysisContext::xrefs,
                               py::return_value_policy::reference_internal);

    // ===== APK layer =====

    py::enum_<Component::Kind>(m, "ComponentKind")
        .value("ACTIVITY", Component::Kind::Activity)
        .value("SERVICE", Component::Kind::Service)
        .value("RECEIVER", Component::Kind::Receiver)
        .value("PROVIDER", Component::Kind::Provider);

    py::class_<IntentFilter>(m, "IntentFilter")
        .def_readonly("actions", &IntentFilter::actions)
        .def_readonly("categories", &IntentFilter::categories)
        .def("has_action", &IntentFilter::has_action, py::arg("name"))
        .def_property_readonly("is_launcher", &IntentFilter::is_launcher);

    py::class_<Component>(m, "Component")
        .def_readonly("kind", &Component::kind)
        .def_readonly("name", &Component::name)
        .def_readonly("exported", &Component::exported)
        .def_readonly("permission", &Component::permission)
        .def_readonly("intent_filters", &Component::intent_filters)
        .def_readonly("guarded_by_signature_permission",
                      &Component::guarded_by_signature_permission)
        .def_property_readonly("is_exported", &Component::is_exported)
        .def_property_readonly("is_reachable", &Component::is_reachable)
        .def("__repr__", [](const Component &c) { return "Component(" + c.name + ")"; });

    py::class_<Manifest>(m, "Manifest")
        .def_property_readonly("package",
                               [](const Manifest &m_) { return std::string(m_.package()); })
        .def_property_readonly("version_code", &Manifest::version_code)
        .def_property_readonly("version_name",
                               [](const Manifest &m_) -> py::object {
                                   auto v = m_.version_name();
                                   if (!v.has_value())
                                       return py::none();
                                   return py::cast(std::string(*v));
                               })
        .def_property_readonly("min_sdk", &Manifest::min_sdk)
        .def_property_readonly("target_sdk", &Manifest::target_sdk)
        .def_property_readonly("permissions", &Manifest::permissions)
        .def_property_readonly("declared_permissions", &Manifest::declared_permissions)
        .def("declared_permission_protection_level",
             &Manifest::declared_permission_protection_level, py::arg("name"))
        .def_property_readonly("components", &Manifest::components)
        .def("components_of",
             [](const Manifest &m_, Component::Kind kind) {
                 std::vector<Component> out;
                 for (const auto *c : m_.components_of(kind))
                     out.push_back(*c);
                 return out;
             })
        .def_property_readonly("launcher_activity",
                               [](const Manifest &m_) -> py::object {
                                   auto v = m_.launcher_activity();
                                   if (!v.has_value())
                                       return py::none();
                                   return py::cast(std::string(*v));
                               })
        .def_property_readonly("debuggable", &Manifest::debuggable)
        .def_property_readonly("allow_backup", &Manifest::allow_backup);

    py::class_<ResourceName>(m, "ResourceName")
        .def_readonly("package", &ResourceName::package)
        .def_readonly("type", &ResourceName::type)
        .def_readonly("entry", &ResourceName::entry)
        .def(py::self == py::self)
        .def("__repr__", [](const ResourceName &n) {
            return "ResourceName(" + n.package + ":" + n.type + "/" + n.entry + ")";
        });

    py::class_<ResourceTable>(m, "ResourceTable")
        .def("name_of",
             [](const ResourceTable &rt, uint32_t id) -> py::object {
                 auto n = rt.name_of(id);
                 if (!n.has_value())
                     return py::none();
                 return py::cast(std::move(*n));
             })
        .def("resolve_string",
             [](const ResourceTable &rt, uint32_t id) -> py::object {
                 auto s = rt.resolve_string(id);
                 if (!s.has_value())
                     return py::none();
                 return py::cast(std::string(*s));
             })
        .def("id_of", &ResourceTable::id_of, py::arg("type"), py::arg("entry"))
        .def_property_readonly("empty", &ResourceTable::empty);

    py::class_<Certificate>(m, "Certificate")
        .def_property_readonly("der",
                               [](const Certificate &c) {
                                   return py::bytes(reinterpret_cast<const char *>(c.der.data()),
                                                    c.der.size());
                               })
        .def_readonly("subject", &Certificate::subject)
        .def_readonly("issuer", &Certificate::issuer)
        .def_readonly("serial_hex", &Certificate::serial_hex)
        .def_readonly("not_before", &Certificate::not_before)
        .def_readonly("not_after", &Certificate::not_after)
        .def_readonly("sha1_hex", &Certificate::sha1_hex)
        .def_readonly("sha256_hex", &Certificate::sha256_hex)
        .def_property_readonly("self_signed", &Certificate::self_signed)
        .def("__repr__", [](const Certificate &c) { return "Certificate(" + c.subject + ")"; });

    py::class_<SigningInfo>(m, "SigningInfo")
        .def_readonly("v1", &SigningInfo::v1)
        .def_readonly("v2", &SigningInfo::v2)
        .def_readonly("v3", &SigningInfo::v3)
        .def_readonly("certificates", &SigningInfo::certificates)
        .def_property_readonly("is_signed", &SigningInfo::is_signed);

    py::class_<Apk>(m, "Apk")
        .def_static(
            "open",
            [](const std::string &path) {
                auto result = Apk::open(path);
                if (!result.has_value()) {
                    if (result.error().code == AnalysisError::Code::FileNotFound)
                        throw DexFileNotFound(result.error().message);
                    throw py::value_error(result.error().message);
                }
                return std::move(result.value());
            },
            py::arg("path"), "Open an APK (ZIP) from disk.")
        .def_property_readonly("entries", &Apk::entries)
        .def("read",
             [](const Apk &apk, std::string_view name) -> py::object {
                 auto data = apk.read(name);
                 if (!data.has_value())
                     return py::none();
                 return py::bytes(reinterpret_cast<const char *>(data->data()), data->size());
             })
        .def_property_readonly("manifest",
                               [](const Apk &apk) -> py::object {
                                   const Manifest *m_ = apk.manifest();
                                   if (!m_)
                                       return py::none();
                                   return py::cast(m_, py::return_value_policy::reference_internal,
                                                   py::cast(apk));
                               })
        .def("analysis",
             [](const Apk &apk) {
                 auto result = apk.analysis();
                 if (!result.has_value())
                     throw py::value_error(result.error().message);
                 return std::move(result.value());
             })
        .def_property_readonly("resources",
                               [](const Apk &apk) -> py::object {
                                   const ResourceTable *rt = apk.resources();
                                   if (!rt)
                                       return py::none();
                                   return py::cast(rt, py::return_value_policy::reference_internal,
                                                   py::cast(apk));
                               })
        .def("resolve_string",
             [](const Apk &apk, uint32_t ref) -> py::object {
                 auto s = apk.resolve_string(ref);
                 if (!s.has_value())
                     return py::none();
                 return py::cast(std::string(*s));
             })
        .def_property_readonly("signing", &Apk::signing,
                               py::return_value_policy::reference_internal);
}
