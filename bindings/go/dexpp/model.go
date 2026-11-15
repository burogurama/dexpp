package dexpp

// The editable DEX model (dex::model): read a DEX file into memory, change it
// a whole class at a time, and write a DEX file again. See
// bindings/c/dexpp_model.h for the C API this wraps.

/*
#include <stdlib.h>
#include "dexpp_model.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Model is an editable DEX file in memory. Read a DEX file into a model,
// change it, and write a DEX file again. From Go the model works on whole
// classes: inspect, copy, move, remove and rename them. Changing the code
// inside a method is only possible from C++.
//
// A Model is separate from the analysis API: it is not a Context. It is a
// handle (see the package documentation). It can be read from several
// goroutines at once, but a change to it must not run at the same time as any
// other use of it.
type Model struct{ ptr *C.DexppModelDex }

func newModel(p *C.DexppModelDex) *Model {
	m := &Model{ptr: p}
	runtime.SetFinalizer(m, (*Model).Close)
	return m
}

// ReadModel reads the bytes of a DEX file into a model. debugInfo keeps line
// numbers and local variable names; false leaves them out, which makes the
// model smaller. It fails with CodeInvalidDex or CodeUnsupportedDex (such as
// the version 41 container format).
func ReadModel(data []byte, debugInfo bool) (*Model, error) {
	var ptr *C.uint8_t
	if len(data) > 0 {
		// cgo lets C read Go memory that holds no Go pointers during the call.
		ptr = (*C.uint8_t)(unsafe.Pointer(&data[0]))
	}
	var cerr *C.DexppError
	p := C.dexpp_model_read(ptr, C.size_t(len(data)), cbool(debugInfo), &cerr)
	runtime.KeepAlive(data)
	if p == nil {
		return nil, goError(cerr)
	}
	return newModel(p), nil
}

// ReadModelFile reads a DEX file into a model. debugInfo is as for ReadModel.
// It fails with CodeFileNotFound, CodeCannotRead, CodeInvalidDex or
// CodeUnsupportedDex.
func ReadModelFile(path string, debugInfo bool) (*Model, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.DexppError
	p := C.dexpp_model_read_file(cpath, cbool(debugInfo), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newModel(p), nil
}

// NewModel makes an empty model of the given DEX format version: 35, 37, 38,
// 39 or 40.
func NewModel(version uint32) *Model {
	return newModel(C.dexpp_model_new(C.uint32_t(version)))
}

// Close frees the model at once. Calling Close again, or on a nil *Model,
// does nothing.
func (m *Model) Close() {
	if m == nil || m.ptr == nil {
		return
	}
	C.dexpp_model_free(m.ptr)
	m.ptr = nil
	runtime.SetFinalizer(m, nil)
}

func (m *Model) p() *C.DexppModelDex {
	if m == nil {
		panicNil("Model")
	}
	if m.ptr == nil {
		panicClosed("Model")
	}
	return m.ptr
}

// Clone returns a full copy of the model. Changing one does not change the
// other.
func (m *Model) Clone() *Model {
	defer runtime.KeepAlive(m)
	return newModel(C.dexpp_model_clone(m.p()))
}

// Write returns the model as the bytes of a DEX file. debugInfo writes line
// numbers and local variable names; false strips them. The model is checked
// first, and the same model always gives the same bytes. It fails with
// CodeInvalidModel (Error.Problems lists the broken rules), CodePoolOverflow
// or CodeVersionTooLow.
func (m *Model) Write(debugInfo bool) ([]byte, error) {
	defer runtime.KeepAlive(m)
	var n C.size_t
	var cerr *C.DexppError
	buf := C.dexpp_model_write(m.p(), cbool(debugInfo), &n, &cerr)
	if buf == nil {
		return nil, goError(cerr)
	}
	defer C.dexpp_buffer_free(buf)
	return goBytes(unsafe.Pointer(buf), n), nil
}

// WriteFile writes the model to a DEX file. debugInfo is as for Write. It
// fails as Write does, and with CodeCannotWrite when the file cannot be
// created or written.
func (m *Model) WriteFile(path string, debugInfo bool) error {
	defer runtime.KeepAlive(m)
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.DexppError
	if C.dexpp_model_write_file(m.p(), cpath, cbool(debugInfo), &cerr) == 0 {
		return goError(cerr)
	}
	return nil
}

// Version returns the DEX format version that Write uses.
func (m *Model) Version() uint32 {
	defer runtime.KeepAlive(m)
	return uint32(C.dexpp_model_version(m.p()))
}

// SetVersion sets the DEX format version that Write uses. Write never raises
// the version by itself: set it when RequiredVersion is higher.
func (m *Model) SetVersion(version uint32) {
	defer runtime.KeepAlive(m)
	C.dexpp_model_set_version(m.p(), C.uint32_t(version))
}

// RequiredVersion returns the lowest DEX version that has every feature the
// model uses.
func (m *Model) RequiredVersion() uint32 {
	defer runtime.KeepAlive(m)
	return uint32(C.dexpp_model_required_version(m.p()))
}

// PoolStats is how many entries each table of the written DEX file would
// have. A DEX file holds at most 65,535 types and prototypes, and at most
// 65,536 fields, methods, method handles and call sites. Use these numbers to
// decide when a file is full.
type PoolStats struct {
	Strings       int // number of strings
	Types         int // number of types; at most 65,535
	Protos        int // number of method prototypes; at most 65,535
	Fields        int // number of field identities; at most 65,536
	Methods       int // number of method identities; at most 65,536
	MethodHandles int // number of method handles
	CallSites     int // number of call sites
}

// PoolStats returns how many entries each table of the written file would
// have.
func (m *Model) PoolStats() PoolStats {
	defer runtime.KeepAlive(m)
	s := C.dexpp_model_pool_stats(m.p())
	return PoolStats{
		Strings:       int(s.strings),
		Types:         int(s.types),
		Protos:        int(s.protos),
		Fields:        int(s.fields),
		Methods:       int(s.methods),
		MethodHandles: int(s.method_handles),
		CallSites:     int(s.call_sites),
	}
}

// Validate returns every rule of the DEX format the model breaks. An empty
// result means Write can encode the model. Otherwise Write fails with an
// *Error whose Problems is the same list.
func (m *Model) Validate() []Problem {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_model_validate(m.p(), &n)
	defer C.dexpp_problem_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	raw := unsafe.Slice(arr, int(n))
	out := make([]Problem, len(raw))
	for i, p := range raw {
		out[i] = Problem{Where: C.GoString(p.where), Message: C.GoString(p.message)}
	}
	return out
}

// ClassCount returns the number of classes in the model.
func (m *Model) ClassCount() int {
	defer runtime.KeepAlive(m)
	return int(C.dexpp_model_class_count(m.p()))
}

// ClassDescriptors returns the descriptor of every class, in model order.
func (m *Model) ClassDescriptors() []string {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_model_class_descriptors(m.p(), &n)
	return takeStringArray(arr, n)
}

// HasClass reports whether the model has a class with this descriptor.
func (m *Model) HasClass(descriptor string) bool {
	defer runtime.KeepAlive(m)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	return C.dexpp_model_has_class(m.p(), cd) != 0
}

// ClassInfo is a summary of one class in a Model.
type ClassInfo struct {
	Descriptor  string      // the class descriptor
	AccessFlags AccessFlags // the class's access flags
	// Superclass is the descriptor of the superclass, or nil for
	// java.lang.Object, which has none.
	Superclass *string
	// SourceFile is the source file name recorded for the class, or nil.
	SourceFile     *string
	InterfaceCount int // number of interfaces the class implements directly
	FieldCount     int // number of fields the class declares
	MethodCount    int // number of methods the class defines
}

// ClassInfo returns a summary of the class with this descriptor. The bool is
// false when the model has no such class.
func (m *Model) ClassInfo(descriptor string) (ClassInfo, bool) {
	defer runtime.KeepAlive(m)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var info C.DexppClassInfo
	if C.dexpp_model_class_info(m.p(), cd, &info) == 0 {
		return ClassInfo{}, false
	}
	defer C.dexpp_class_info_clear(&info)
	return ClassInfo{
		Descriptor:     C.GoString(info.descriptor),
		AccessFlags:    AccessFlags(info.access_flags),
		Superclass:     goOptString(info.superclass),
		SourceFile:     goOptString(info.source_file),
		InterfaceCount: int(info.interface_count),
		FieldCount:     int(info.field_count),
		MethodCount:    int(info.method_count),
	}, true
}

// ClassInterfaces returns the descriptors of the interfaces the class
// implements directly. It is nil when the model has no such class.
func (m *Model) ClassInterfaces(descriptor string) []string {
	defer runtime.KeepAlive(m)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var n C.size_t
	arr := C.dexpp_model_class_interfaces(m.p(), cd, &n)
	return takeStringArray(arr, n)
}

// MoveClass takes the class out of src and puts it into m. The class is not
// copied, so this is cheap for a large class. It fails with CodeModel when
// src has no such class or m already has one.
func (m *Model) MoveClass(src *Model, descriptor string) error {
	defer runtime.KeepAlive(m)
	defer runtime.KeepAlive(src)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var cerr *C.DexppError
	if C.dexpp_model_move_class(m.p(), src.p(), cd, &cerr) == 0 {
		return goError(cerr)
	}
	return nil
}

// CopyClass copies the class from src into m. src is not changed. It fails
// with CodeModel when src has no such class or m already has one.
func (m *Model) CopyClass(src *Model, descriptor string) error {
	defer runtime.KeepAlive(m)
	defer runtime.KeepAlive(src)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var cerr *C.DexppError
	if C.dexpp_model_copy_class(m.p(), src.p(), cd, &cerr) == 0 {
		return goError(cerr)
	}
	return nil
}

// RemoveClass removes the class with this descriptor. It reports whether a
// class was removed.
func (m *Model) RemoveClass(descriptor string) bool {
	defer runtime.KeepAlive(m)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	return C.dexpp_model_remove_class(m.p(), cd) != 0
}

// RenameTypes renames classes everywhere they are referenced. The mapping
// goes from old descriptor to new descriptor. Every reference to a class in
// it is changed: the class itself, superclasses and interfaces, method and
// field identities, array types, instructions, catch types, annotations, and
// the class names inside generic signatures. A class that is not in the
// mapping is left as it is. String constants are never changed, so a name
// passed to Class.forName keeps its old value.
func (m *Model) RenameTypes(mapping map[string]string) {
	defer runtime.KeepAlive(m)
	p := m.p()
	if len(mapping) == 0 {
		return
	}
	from := make([]string, 0, len(mapping))
	to := make([]string, 0, len(mapping))
	for k, v := range mapping {
		from = append(from, k)
		to = append(to, v)
	}
	cfrom, cto := cStrings(from), cStrings(to)
	defer freeCStrings(cfrom)
	defer freeCStrings(cto)
	// cgo allows passing &cfrom[0]: the slices hold C pointers, not Go
	// pointers.
	C.dexpp_model_rename_types(p, &cfrom[0], &cto[0], C.size_t(len(cfrom)))
}
