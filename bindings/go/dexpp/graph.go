package dexpp

// The whole-program analyses: the call graph, the class hierarchy and the
// cross-reference index.

/*
#include <stdlib.h>
#include "dexpp.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// ---- Call graph ---------------------------------------------------------- //

// CallGraph is the call graph of the whole program: which method can call
// which. Get it from Context.CallGraph. It is a handle (see the package
// documentation).
//
// Nodes and edges are numbered. A CallNode or a CallEdge is a plain copy that
// holds the numbers of its neighbours, and the methods of CallGraph take and
// return these numbers. Nodes and Edges copy the whole table in one call:
// call them once and index the slice. Node and Edge copy one entry, which is
// cheaper when only a few are needed:
//
//	n, _ := g.Find(id)
//	for _, caller := range g.Callers(n.Index) {
//		c, _ := g.Node(caller)
//		fmt.Println(c.ID)
//	}
//
// Virtual dispatch is not stored as edges. A call site has one EdgeDeclared
// edge, to the method the bytecode names, and at most one
// EdgeInheritedResolution edge. The methods a virtual or interface call may
// also run are the dispatch targets of the declared callee: see
// DispatchTargets. Callees and Callers include them.
type CallGraph struct{ ptr *C.DexppCallGraph }

// CallNode is a method in the call graph. A node is resolved when the method
// is defined in a loaded file. A method the program calls but does not
// contain, such as a framework method, is a node too, with Resolved false.
type CallNode struct {
	Index    uint32   // position of the node in the graph
	ID       MethodID // the method's identity
	Resolved bool     // true when the method is defined in a loaded file
}

// CallEdge is one call site: an invoke instruction in a caller, pointing at a
// callee. A call site has one EdgeDeclared edge, and a second,
// EdgeInheritedResolution edge when the class the bytecode names does not
// define the method itself. Both edges have the same caller, kind and offset.
type CallEdge struct {
	Index      uint32     // position of the edge in the graph
	CallerNode uint32     // index of the calling method's node
	CalleeNode uint32     // index of the called method's node
	Kind       InvokeKind // the kind of invoke instruction
	CodeOffset uint32     // offset of the invoke instruction in the caller's code
	Origin     EdgeOrigin // the declared target, or the one found by walking up the hierarchy
}

// Close frees the call-graph handle at once. The graph itself stays in the
// analysis, so Context.CallGraph returns it again without rebuilding it.
// Calling Close again, or on a nil *CallGraph, does nothing.
func (g *CallGraph) Close() {
	if g == nil || g.ptr == nil {
		return
	}
	C.dexpp_call_graph_free(g.ptr)
	g.ptr = nil
	runtime.SetFinalizer(g, nil)
}

func (g *CallGraph) p() *C.DexppCallGraph {
	if g == nil {
		panicNil("CallGraph")
	}
	if g.ptr == nil {
		panicClosed("CallGraph")
	}
	return g.ptr
}

func goCallNode(n *C.DexppCallNode) CallNode {
	return CallNode{Index: uint32(n.index), ID: goMethodID(n.id), Resolved: n.resolved != 0}
}

func goCallEdge(index uint32, e *C.DexppCallEdge) CallEdge {
	return CallEdge{
		Index:      index,
		CallerNode: uint32(e.caller_node),
		CalleeNode: uint32(e.callee_node),
		Kind:       InvokeKind(e.kind),
		CodeOffset: uint32(e.code_offset),
		Origin:     EdgeOrigin(e.origin),
	}
}

// NodeCount returns the number of nodes.
func (g *CallGraph) NodeCount() int {
	defer runtime.KeepAlive(g)
	return int(C.dexpp_call_graph_node_count(g.p()))
}

// EdgeCount returns the number of edges.
func (g *CallGraph) EdgeCount() int {
	defer runtime.KeepAlive(g)
	return int(C.dexpp_call_graph_edge_count(g.p()))
}

// Empty reports whether the graph has no nodes.
func (g *CallGraph) Empty() bool { return g.NodeCount() == 0 }

// Nodes returns every node, in index order, so Nodes()[i].Index == i. It
// copies the whole table: call it once, not in a loop.
func (g *CallGraph) Nodes() []CallNode {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_nodes(g.p(), &n)
	defer C.dexpp_call_node_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]CallNode, len(raw))
	for i := range raw {
		out[i] = goCallNode(&raw[i])
	}
	return out
}

// Node returns the node with this index. The bool is false when the index is
// out of range.
func (g *CallGraph) Node(index uint32) (CallNode, bool) {
	defer runtime.KeepAlive(g)
	var out C.DexppCallNode
	if C.dexpp_call_graph_node(g.p(), C.uint32_t(index), &out) == 0 {
		return CallNode{}, false
	}
	defer C.dexpp_call_node_clear(&out)
	return goCallNode(&out), true
}

// Edges returns every edge, in index order, so Edges()[i].Index == i. It
// copies the whole table: call it once, not in a loop.
func (g *CallGraph) Edges() []CallEdge {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_edges(g.p(), &n)
	defer C.dexpp_call_edge_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]CallEdge, len(raw))
	for i := range raw {
		out[i] = goCallEdge(uint32(i), &raw[i])
	}
	return out
}

// Edge returns the edge with this index. The bool is false when the index is
// out of range.
func (g *CallGraph) Edge(index uint32) (CallEdge, bool) {
	defer runtime.KeepAlive(g)
	var out C.DexppCallEdge
	if C.dexpp_call_graph_edge(g.p(), C.uint32_t(index), &out) == 0 {
		return CallEdge{}, false
	}
	return goCallEdge(index, &out), true
}

// Find returns the node of the method with this identity. The bool is false
// when the graph has no such method.
func (g *CallGraph) Find(id MethodID) (CallNode, bool) {
	defer runtime.KeepAlive(g)
	cls, name, proto := C.CString(id.ClassDescriptor), C.CString(id.Name), C.CString(id.Proto)
	defer C.free(unsafe.Pointer(cls))
	defer C.free(unsafe.Pointer(name))
	defer C.free(unsafe.Pointer(proto))
	index := int64(C.dexpp_call_graph_find(g.p(), cls, name, proto))
	if index < 0 {
		return CallNode{}, false
	}
	return g.Node(uint32(index))
}

// OutEdges returns the indices of the edges that leave the node: the call
// sites in this method. It is nil when the index is out of range.
func (g *CallGraph) OutEdges(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_out_edges(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// InEdges returns the indices of the edges that enter the node: the calls
// into this method. It is nil when the index is out of range.
func (g *CallGraph) InEdges(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_in_edges(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// DispatchTargets returns the indices of the other methods that a virtual or
// interface call declared against this node may run: the overrides in loaded
// subclasses and implementers, and what subclasses that do not override it
// inherit. The list is sorted by index and stored once on the node, shared by
// all its call sites. Every target is a resolved node. It is nil when no such
// call names this method.
func (g *CallGraph) DispatchTargets(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_dispatch_targets(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// DispatchSources is the reverse of DispatchTargets: the indices of the
// declared methods through which a virtual or interface call can reach this
// node.
func (g *CallGraph) DispatchSources(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_dispatch_sources(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// Callees returns the indices of every method this node may call: the callee
// of each outgoing edge, plus the dispatch targets of each virtual and
// interface call. The list is sorted and has no duplicates.
func (g *CallGraph) Callees(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_callees(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// Callers returns the indices of every method that may call this node,
// directly or through virtual dispatch. The list is sorted and has no
// duplicates.
func (g *CallGraph) Callers(node uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_callers(g.p(), C.uint32_t(node), &n)
	return takeIndexArray(arr, n)
}

// EdgeDispatchTargets returns the dispatch targets that apply at one edge:
// those of its callee for an EdgeDeclared edge of an invoke-virtual or
// invoke-interface, and none for every other edge.
func (g *CallGraph) EdgeDispatchTargets(edge uint32) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_edge_dispatch_targets(g.p(), C.uint32_t(edge), &n)
	return takeIndexArray(arr, n)
}

// EdgesWhere returns the indices of the edges that match both filters. Pass
// AnyEdgeOrigin or AnyInvokeKind to leave a filter out. The filtering runs in
// the library, which is much faster than copying every edge into Go first.
// EdgesWhere(EdgeDeclared, AnyInvokeKind) gives exactly one edge per call
// site.
func (g *CallGraph) EdgesWhere(origin EdgeOrigin, kind InvokeKind) []uint32 {
	defer runtime.KeepAlive(g)
	var n C.size_t
	arr := C.dexpp_call_graph_edges_where(g.p(), C.int(origin), C.int(kind), &n)
	return takeIndexArray(arr, n)
}

// MethodFor returns the method behind a resolved node. It is nil for an
// external node (Resolved false) and for an index out of range.
func (g *CallGraph) MethodFor(node uint32) *Method {
	defer runtime.KeepAlive(g)
	p := C.dexpp_call_graph_method_for(g.p(), C.uint32_t(node))
	if p == nil {
		return nil
	}
	return newMethod(p)
}

// ---- Class hierarchy ----------------------------------------------------- //

// ClassHierarchy is inheritance over the whole program. Get it from
// Context.ClassHierarchy. It is a handle (see the package documentation).
//
// Every query takes and returns class descriptors. A class outside the loaded
// files is in the hierarchy when a loaded class names it as its superclass or
// as an interface; its own ancestors are not known. A query about a
// descriptor the hierarchy has never seen gives nil or false.
type ClassHierarchy struct{ ptr *C.DexppClassHierarchy }

// Close frees the hierarchy handle at once. The hierarchy itself stays in the
// analysis. Calling Close again, or on a nil *ClassHierarchy, does nothing.
func (h *ClassHierarchy) Close() {
	if h == nil || h.ptr == nil {
		return
	}
	C.dexpp_class_hierarchy_free(h.ptr)
	h.ptr = nil
	runtime.SetFinalizer(h, nil)
}

func (h *ClassHierarchy) p() *C.DexppClassHierarchy {
	if h == nil {
		panicNil("ClassHierarchy")
	}
	if h.ptr == nil {
		panicClosed("ClassHierarchy")
	}
	return h.ptr
}

// query runs one C query that takes a descriptor and returns a string array.
func (h *ClassHierarchy) query(descriptor string,
	fn func(*C.DexppClassHierarchy, *C.char, *C.size_t) **C.char) []string {
	defer runtime.KeepAlive(h)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var n C.size_t
	arr := fn(h.p(), cd, &n)
	return takeStringArray(arr, n)
}

// Supertypes returns the direct superclass, then the directly implemented
// interfaces.
func (h *ClassHierarchy) Supertypes(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_supertypes(p, d, n)
	})
}

// SuperclassOf returns the direct superclass. The bool is false when none is
// recorded: for java.lang.Object, for a class outside the loaded files, and
// for a descriptor the hierarchy does not know.
func (h *ClassHierarchy) SuperclassOf(descriptor string) (string, bool) {
	defer runtime.KeepAlive(h)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	return takeOptString(C.dexpp_class_hierarchy_superclass_of(h.p(), cd))
}

// DirectInterfaces returns the interfaces the class implements directly, or,
// for an interface, the interfaces it extends directly.
func (h *ClassHierarchy) DirectInterfaces(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_direct_interfaces(p, d, n)
	})
}

// Subclasses returns the classes that directly extend the class.
func (h *ClassHierarchy) Subclasses(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_subclasses(p, d, n)
	})
}

// Implementers returns the classes that directly implement the interface.
func (h *ClassHierarchy) Implementers(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_implementers(p, d, n)
	})
}

// AllSupertypes returns every ancestor: superclasses and interfaces, at any
// distance. The class itself is not included.
func (h *ClassHierarchy) AllSupertypes(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_all_supertypes(p, d, n)
	})
}

// AllDescendants returns every class below this one, at any distance, through
// both extends and implements. The class itself is not included.
func (h *ClassHierarchy) AllDescendants(descriptor string) []string {
	return h.query(descriptor, func(p *C.DexppClassHierarchy, d *C.char, n *C.size_t) **C.char {
		return C.dexpp_class_hierarchy_all_descendants(p, d, n)
	})
}

// IsSubtypeOf reports whether sub is super itself or has super among its
// ancestors.
func (h *ClassHierarchy) IsSubtypeOf(sub, super string) bool {
	defer runtime.KeepAlive(h)
	cs, cp := C.CString(sub), C.CString(super)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cp))
	return C.dexpp_class_hierarchy_is_subtype_of(h.p(), cs, cp) != 0
}

// IsLoaded reports whether the class is defined in a loaded file.
func (h *ClassHierarchy) IsLoaded(descriptor string) bool {
	defer runtime.KeepAlive(h)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	return C.dexpp_class_hierarchy_is_loaded(h.p(), cd) != 0
}

// IsInterface reports whether the class is defined in a loaded file and is an
// interface. It is false for a class outside the loaded files, because its
// kind is not known.
func (h *ClassHierarchy) IsInterface(descriptor string) bool {
	defer runtime.KeepAlive(h)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	return C.dexpp_class_hierarchy_is_interface(h.p(), cd) != 0
}

// Empty reports whether the hierarchy has no classes.
func (h *ClassHierarchy) Empty() bool {
	defer runtime.KeepAlive(h)
	return C.dexpp_class_hierarchy_empty(h.p()) != 0
}

// ---- Cross-references ---------------------------------------------------- //

// Xrefs is the cross-reference index of the whole program: who uses a method,
// a field, a string or a type. Get it from Context.Xrefs. It is a handle (see
// the package documentation).
//
// Every query returns a list of sites. A site has a CodeOffset and a
// Referrer: the number of the method whose code holds the instruction.
// ReferrerID and ReferrerMethod turn that number into the method. Only
// instructions are indexed.
type Xrefs struct{ ptr *C.DexppXrefs }

// MethodXref is one invoke instruction that calls a method.
type MethodXref struct {
	Referrer   uint32     // number of the method that holds the instruction
	CodeOffset uint32     // offset of the instruction, in code units
	Kind       InvokeKind // the kind of invoke instruction
}

// FieldXref is one instruction that reads or writes a field.
type FieldXref struct {
	Referrer   uint32 // number of the method that holds the instruction
	CodeOffset uint32 // offset of the instruction, in code units
	IsWrite    bool   // true for a write, false for a read
	IsStatic   bool   // true for a static field access
}

// StringXref is one const-string instruction.
type StringXref struct {
	Referrer   uint32 // number of the method that holds the instruction
	CodeOffset uint32 // offset of the instruction, in code units
}

// TypeXref is one instruction that names a type.
type TypeXref struct {
	Referrer   uint32      // number of the method that holds the instruction
	CodeOffset uint32      // offset of the instruction, in code units
	Kind       TypeUseKind // what the instruction does with the type
}

// MethodRefGroup is the call sites of one called method, as
// Xrefs.MethodRefsByName finds them.
type MethodRefGroup struct {
	Target MethodID     // the called method
	Sites  []MethodXref // its call sites
}

// Close frees the cross-reference handle at once. The index itself stays in
// the analysis. Calling Close again, or on a nil *Xrefs, does nothing.
func (x *Xrefs) Close() {
	if x == nil || x.ptr == nil {
		return
	}
	C.dexpp_xrefs_free(x.ptr)
	x.ptr = nil
	runtime.SetFinalizer(x, nil)
}

func (x *Xrefs) p() *C.DexppXrefs {
	if x == nil {
		panicNil("Xrefs")
	}
	if x.ptr == nil {
		panicClosed("Xrefs")
	}
	return x.ptr
}

// ReferrerCount returns the number of referrers: the methods that hold at
// least one indexed instruction.
func (x *Xrefs) ReferrerCount() int {
	defer runtime.KeepAlive(x)
	return int(C.dexpp_xrefs_referrer_count(x.p()))
}

// Referrers returns the identity of every referrer, in number order, so
// Referrers()[site.Referrer] is the method a site is in. It copies the whole
// table: call it once, not in a loop.
func (x *Xrefs) Referrers() []MethodID {
	defer runtime.KeepAlive(x)
	var n C.size_t
	arr := C.dexpp_xrefs_referrers(x.p(), &n)
	defer C.dexpp_method_id_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]MethodID, len(raw))
	for i := range raw {
		out[i] = goMethodID(raw[i])
	}
	return out
}

// ReferrerID returns the identity of one referrer. The bool is false when the
// number is out of range.
func (x *Xrefs) ReferrerID(referrer uint32) (MethodID, bool) {
	defer runtime.KeepAlive(x)
	var out C.DexppMethodId
	if C.dexpp_xrefs_referrer_id(x.p(), C.uint32_t(referrer), &out) == 0 {
		return MethodID{}, false
	}
	defer C.dexpp_method_id_clear(&out)
	return goMethodID(out), true
}

// ReferrerMethod returns the method of one referrer, or nil when the number
// is out of range.
func (x *Xrefs) ReferrerMethod(referrer uint32) *Method {
	defer runtime.KeepAlive(x)
	p := C.dexpp_xrefs_referrer_method(x.p(), C.uint32_t(referrer))
	if p == nil {
		return nil
	}
	return newMethod(p)
}

func goMethodXrefs(arr *C.DexppMethodXref, n C.size_t) []MethodXref {
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]MethodXref, len(raw))
	for i, s := range raw {
		out[i] = MethodXref{uint32(s.referrer), uint32(s.code_offset), InvokeKind(s.kind)}
	}
	return out
}

// MethodRefs returns the call sites whose target, as the bytecode names it,
// is exactly this method. Virtual dispatch is not applied: a call to
// Animal.sound is not listed under Dog.sound. The call graph applies it.
func (x *Xrefs) MethodRefs(id MethodID) []MethodXref {
	defer runtime.KeepAlive(x)
	cls, name, proto := C.CString(id.ClassDescriptor), C.CString(id.Name), C.CString(id.Proto)
	defer C.free(unsafe.Pointer(cls))
	defer C.free(unsafe.Pointer(name))
	defer C.free(unsafe.Pointer(proto))
	var n C.size_t
	arr := C.dexpp_xrefs_method_refs(x.p(), cls, name, proto, &n)
	defer C.dexpp_method_xref_array_free(arr, n)
	return goMethodXrefs(arr, n)
}

// MethodRefsByName returns the call sites of every method with this name, in
// any class and with any prototype, grouped by called method. The groups come
// in no fixed order. It is the quick way to find every call to a name such as
// "exec" or "loadUrl".
func (x *Xrefs) MethodRefsByName(name string) []MethodRefGroup {
	defer runtime.KeepAlive(x)
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	var n C.size_t
	arr := C.dexpp_xrefs_method_refs_by_name(x.p(), cname, &n)
	defer C.dexpp_method_ref_group_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]MethodRefGroup, len(raw))
	for i := range raw {
		out[i] = MethodRefGroup{
			Target: goMethodID(raw[i].target),
			Sites:  goMethodXrefs(raw[i].sites, raw[i].site_count),
		}
	}
	return out
}

// fieldQuery runs one of the three C field queries.
func (x *Xrefs) fieldQuery(id FieldID, fn func(*C.DexppXrefs, *C.char, *C.char, *C.char,
	*C.size_t) *C.DexppFieldXref) []FieldXref {
	defer runtime.KeepAlive(x)
	cls, name, typ := C.CString(id.ClassDescriptor), C.CString(id.Name), C.CString(id.Type)
	defer C.free(unsafe.Pointer(cls))
	defer C.free(unsafe.Pointer(name))
	defer C.free(unsafe.Pointer(typ))
	var n C.size_t
	arr := fn(x.p(), cls, name, typ, &n)
	defer C.dexpp_field_xref_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]FieldXref, len(raw))
	for i, s := range raw {
		out[i] = FieldXref{uint32(s.referrer), uint32(s.code_offset), s.is_write != 0, s.is_static != 0}
	}
	return out
}

// FieldRefs returns every read and write of this field.
func (x *Xrefs) FieldRefs(id FieldID) []FieldXref {
	return x.fieldQuery(id, func(p *C.DexppXrefs, c, n, t *C.char, count *C.size_t) *C.DexppFieldXref {
		return C.dexpp_xrefs_field_refs(p, c, n, t, count)
	})
}

// FieldReads returns only the reads of this field.
func (x *Xrefs) FieldReads(id FieldID) []FieldXref {
	return x.fieldQuery(id, func(p *C.DexppXrefs, c, n, t *C.char, count *C.size_t) *C.DexppFieldXref {
		return C.dexpp_xrefs_field_reads(p, c, n, t, count)
	})
}

// FieldWrites returns only the writes of this field.
func (x *Xrefs) FieldWrites(id FieldID) []FieldXref {
	return x.fieldQuery(id, func(p *C.DexppXrefs, c, n, t *C.char, count *C.size_t) *C.DexppFieldXref {
		return C.dexpp_xrefs_field_writes(p, c, n, t, count)
	})
}

// StringRefs returns the const-string instructions that load exactly this
// string. The string may contain NUL bytes. To search by substring or
// pattern, filter ReferencedStrings first.
func (x *Xrefs) StringRefs(value string) []StringXref {
	defer runtime.KeepAlive(x)
	cv := C.CString(value) // copies every byte, NUL bytes included
	defer C.free(unsafe.Pointer(cv))
	var n C.size_t
	arr := C.dexpp_xrefs_string_refs(x.p(), cv, C.size_t(len(value)), &n)
	defer C.dexpp_string_xref_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]StringXref, len(raw))
	for i, s := range raw {
		out[i] = StringXref{uint32(s.referrer), uint32(s.code_offset)}
	}
	return out
}

// ReferencedStrings returns every distinct string that some const-string
// instruction loads.
func (x *Xrefs) ReferencedStrings() []string {
	defer runtime.KeepAlive(x)
	var n C.size_t
	arr := C.dexpp_xrefs_referenced_strings(x.p(), &n)
	return takeTextArray(arr, n)
}

// TypeRefs returns the instructions that name this type: const-class,
// check-cast, instance-of, new-instance, new-array and filled-new-array.
func (x *Xrefs) TypeRefs(descriptor string) []TypeXref {
	defer runtime.KeepAlive(x)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var n C.size_t
	arr := C.dexpp_xrefs_type_refs(x.p(), cd, &n)
	defer C.dexpp_type_xref_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]TypeXref, len(raw))
	for i, s := range raw {
		out[i] = TypeXref{uint32(s.referrer), uint32(s.code_offset), TypeUseKind(s.kind)}
	}
	return out
}

// Empty reports whether nothing is indexed.
func (x *Xrefs) Empty() bool {
	defer runtime.KeepAlive(x)
	return C.dexpp_xrefs_empty(x.p()) != 0
}
