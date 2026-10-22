package dexpp

// The editable DEX model (dex::model): read a DEX file into memory, change it
// at the granularity of whole classes, and write a DEX file again. See
// bindings/c/dexpp_model.h for the boundary this wraps.

/*
#include <stdlib.h>
#include "dexpp_model.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Model is an editable in-memory DEX file. It owns a C-side model freed by a
// finalizer on garbage collection; call Close to release it eagerly.
type Model struct{ ptr *C.DexppModelDex }

func newModel(p *C.DexppModelDex) *Model {
	m := &Model{ptr: p}
	runtime.SetFinalizer(m, (*Model).Close)
	return m
}

// ReadModel reads DEX bytes into a model. debugInfo keeps line numbers and
// local variables.
func ReadModel(data []byte, debugInfo bool) (*Model, error) {
	var ptr *C.uint8_t
	if len(data) > 0 {
		ptr = (*C.uint8_t)(unsafe.Pointer(&data[0]))
	}
	var cerr *C.char
	p := C.dexpp_model_read(ptr, C.size_t(len(data)), cbool(debugInfo), &cerr)
	runtime.KeepAlive(data)
	if p == nil {
		return nil, goError(cerr)
	}
	return newModel(p), nil
}

// ReadModelFile reads a DEX file into a model.
func ReadModelFile(path string, debugInfo bool) (*Model, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.char
	p := C.dexpp_model_read_file(cpath, cbool(debugInfo), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newModel(p), nil
}

// NewModel makes an empty model of the given DEX version (35, 37, 38, 39, 40).
func NewModel(version uint32) *Model {
	return newModel(C.dexpp_model_new(C.uint32_t(version)))
}

// Clone returns a deep copy of the model.
func (m *Model) Clone() *Model {
	p := C.dexpp_model_clone(m.ptr)
	runtime.KeepAlive(m)
	return newModel(p)
}

// Close releases the model eagerly. Safe to call more than once.
func (m *Model) Close() {
	if m.ptr != nil {
		C.dexpp_model_free(m.ptr)
		m.ptr = nil
		runtime.SetFinalizer(m, nil)
	}
}

// Write returns the model as DEX bytes. debugInfo writes debug information.
func (m *Model) Write(debugInfo bool) ([]byte, error) {
	var n C.size_t
	var cerr *C.char
	buf := C.dexpp_model_write(m.ptr, cbool(debugInfo), &n, &cerr)
	runtime.KeepAlive(m)
	if buf == nil {
		return nil, goError(cerr)
	}
	defer C.dexpp_buffer_free(buf)
	return C.GoBytes(unsafe.Pointer(buf), C.int(n)), nil
}

// WriteFile writes the model to a DEX file.
func (m *Model) WriteFile(path string, debugInfo bool) error {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.char
	ok := C.dexpp_model_write_file(m.ptr, cpath, cbool(debugInfo), &cerr)
	runtime.KeepAlive(m)
	if ok == 0 {
		return goError(cerr)
	}
	return nil
}

// Version is the model's DEX format version.
func (m *Model) Version() uint32 {
	v := uint32(C.dexpp_model_version(m.ptr))
	runtime.KeepAlive(m)
	return v
}

// SetVersion sets the DEX format version write() will use.
func (m *Model) SetVersion(version uint32) {
	C.dexpp_model_set_version(m.ptr, C.uint32_t(version))
	runtime.KeepAlive(m)
}

// RequiredVersion is the lowest DEX version with every feature the model uses.
func (m *Model) RequiredVersion() uint32 {
	v := uint32(C.dexpp_model_required_version(m.ptr))
	runtime.KeepAlive(m)
	return v
}

// PoolStats is how many entries each pool of the written file would have.
type PoolStats struct {
	Strings       int
	Types         int
	Protos        int
	Fields        int
	Methods       int
	MethodHandles int
	CallSites     int
}

// PoolStats reports the pool sizes the model would write, to help decide how
// to split classes across files.
func (m *Model) PoolStats() PoolStats {
	s := C.dexpp_model_pool_stats(m.ptr)
	runtime.KeepAlive(m)
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

// Problem is one rule of the format the model breaks. Where is empty for a
// whole-file problem.
type Problem struct {
	Where   string
	Message string
}

// Validate returns every rule of the format the model breaks; an empty slice
// means write() can encode it.
func (m *Model) Validate() []Problem {
	var n C.size_t
	arr := C.dexpp_model_validate(m.ptr, &n)
	runtime.KeepAlive(m)
	if n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]Problem, int(n))
	for i := range out {
		out[i] = Problem{Where: C.GoString(slice[i].where), Message: C.GoString(slice[i].message)}
	}
	C.dexpp_problem_array_free(arr, n)
	return out
}

// ClassCount is the number of classes in the model.
func (m *Model) ClassCount() int {
	n := int(C.dexpp_model_class_count(m.ptr))
	runtime.KeepAlive(m)
	return n
}

// ClassDescriptors returns every class's descriptor, in model order.
func (m *Model) ClassDescriptors() []string {
	var n C.size_t
	arr := C.dexpp_model_class_descriptors(m.ptr, &n)
	runtime.KeepAlive(m)
	return takeStringArray(arr, n)
}

// HasClass reports whether the model has a class with this descriptor.
func (m *Model) HasClass(descriptor string) bool {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	has := C.dexpp_model_has_class(m.ptr, cd) != 0
	runtime.KeepAlive(m)
	return has
}

// ClassInfo holds facts about one class. Superclass and SourceFile are nil
// when the class has none.
type ClassInfo struct {
	Descriptor     string
	AccessFlags    uint32
	Superclass     *string
	SourceFile     *string
	InterfaceCount int
	FieldCount     int
	MethodCount    int
}

// ClassInfo returns facts about the class with this descriptor, and whether it
// exists.
func (m *Model) ClassInfo(descriptor string) (ClassInfo, bool) {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var info C.DexppClassInfo
	ok := C.dexpp_model_class_info(m.ptr, cd, &info)
	runtime.KeepAlive(m)
	if ok == 0 {
		return ClassInfo{}, false
	}
	defer C.dexpp_class_info_free(&info)
	return ClassInfo{
		Descriptor:     C.GoString(info.descriptor),
		AccessFlags:    uint32(info.access_flags),
		Superclass:     optString(info.superclass),
		SourceFile:     optString(info.source_file),
		InterfaceCount: int(info.interface_count),
		FieldCount:     int(info.field_count),
		MethodCount:    int(info.method_count),
	}, true
}

// ClassInterfaces returns the interface descriptors the class implements.
func (m *Model) ClassInterfaces(descriptor string) []string {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var n C.size_t
	arr := C.dexpp_model_class_interfaces(m.ptr, cd, &n)
	runtime.KeepAlive(m)
	return takeStringArray(arr, n)
}

// MoveClass moves the class out of src and into m. The class is moved, not
// copied, so this is cheap for a large class.
func (m *Model) MoveClass(src *Model, descriptor string) error {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var cerr *C.char
	ok := C.dexpp_model_move_class(m.ptr, src.ptr, cd, &cerr)
	runtime.KeepAlive(m)
	runtime.KeepAlive(src)
	if ok == 0 {
		return goError(cerr)
	}
	return nil
}

// CopyClass copies the class from src into m, leaving src unchanged.
func (m *Model) CopyClass(src *Model, descriptor string) error {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	var cerr *C.char
	ok := C.dexpp_model_copy_class(m.ptr, src.ptr, cd, &cerr)
	runtime.KeepAlive(m)
	runtime.KeepAlive(src)
	if ok == 0 {
		return goError(cerr)
	}
	return nil
}

// RemoveClass removes the class with this descriptor, reporting whether one
// was removed.
func (m *Model) RemoveClass(descriptor string) bool {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	removed := C.dexpp_model_remove_class(m.ptr, cd) != 0
	runtime.KeepAlive(m)
	return removed
}

// RenameTypes renames every reference to each class in the mapping (from its
// descriptor to the new one). A descriptor not in the mapping is unchanged;
// string constants are never renamed.
func (m *Model) RenameTypes(mapping map[string]string) {
	if len(mapping) == 0 {
		return
	}
	from := make([]*C.char, 0, len(mapping))
	to := make([]*C.char, 0, len(mapping))
	defer func() {
		for i := range from {
			C.free(unsafe.Pointer(from[i]))
			C.free(unsafe.Pointer(to[i]))
		}
	}()
	for k, v := range mapping {
		from = append(from, C.CString(k))
		to = append(to, C.CString(v))
	}
	C.dexpp_model_rename_types(m.ptr, &from[0], &to[0], C.size_t(len(from)))
	runtime.KeepAlive(m)
}

func cbool(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

// optString copies a malloc'd C string to Go (nil stays nil) and frees it.
func optString(s *C.char) *string {
	if s == nil {
		return nil
	}
	out := C.GoString(s)
	return &out
}

// takeStringArray copies a malloc'd char* array of n strings to Go and frees
// the whole array with the dexpp allocator.
func takeStringArray(arr **C.char, n C.size_t) []string {
	if n == 0 {
		C.dexpp_string_array_free(arr, n)
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]string, int(n))
	for i := range out {
		out[i] = C.GoString(slice[i])
	}
	C.dexpp_string_array_free(arr, n)
	return out
}
