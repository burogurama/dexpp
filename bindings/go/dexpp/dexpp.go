// Package dexpp is a Go binding for the dexpp Android DEX/APK static-analysis
// library, via its C ABI (bindings/c) and cgo.
//
// Build prerequisite: the dexpp static libraries must be built first:
//
//	cmake -S . -B build && cmake --build build --target dexpp_c -j
//
// which produces build/libdexpp_c.a and build/libdexpp_core.a that the cgo
// LDFLAGS below link against.
//
// Handles (Context, Apk, Class, Method) own a C-side object freed by a
// finalizer on garbage collection; call Close to release eagerly. Strings are
// copied to Go and need no cleanup.
package dexpp

/*
#cgo CFLAGS: -I${SRCDIR}/../../c
#cgo LDFLAGS: ${SRCDIR}/../../../build/libdexpp_c.a ${SRCDIR}/../../../build/libdexpp_core.a -lstdc++
#include <stdlib.h>
#include "dexpp.h"
*/
import "C"

import (
	"errors"
	"runtime"
	"unsafe"
)

// ComponentKind identifies an Android manifest component kind.
type ComponentKind int

const (
	Activity ComponentKind = iota
	Service
	Receiver
	Provider
)

func (k ComponentKind) String() string {
	switch k {
	case Activity:
		return "activity"
	case Service:
		return "service"
	case Receiver:
		return "receiver"
	case Provider:
		return "provider"
	default:
		return "unknown"
	}
}

func goError(err *C.char) error {
	if err == nil {
		return errors.New("dexpp: unknown error")
	}
	defer C.dexpp_string_free(err)
	return errors.New(C.GoString(err))
}

// takeString consumes a malloc'd C string, copies it to Go, and frees it.
func takeString(s *C.char) string {
	if s == nil {
		return ""
	}
	defer C.dexpp_string_free(s)
	return C.GoString(s)
}

// ---- Context ------------------------------------------------------------- //

// Context is a code-analysis view over one or more DEX files.
type Context struct{ ptr *C.DexppContext }

// FromAPK builds an analysis context over every classes*.dex in an APK.
func FromAPK(path string) (*Context, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.char
	p := C.dexpp_context_from_apk(cpath, &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

// FromDEX builds an analysis context over a single DEX file.
func FromDEX(path string) (*Context, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.char
	p := C.dexpp_context_from_dex(cpath, &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

func newContext(p *C.DexppContext) *Context {
	c := &Context{ptr: p}
	runtime.SetFinalizer(c, (*Context).Close)
	return c
}

// Close releases the context eagerly. Safe to call more than once.
func (c *Context) Close() {
	if c.ptr != nil {
		C.dexpp_context_free(c.ptr)
		c.ptr = nil
		runtime.SetFinalizer(c, nil)
	}
}

// Classes returns every loaded class.
func (c *Context) Classes() []*Class {
	var n C.size_t
	arr := C.dexpp_context_classes(c.ptr, &n)
	defer C.free(unsafe.Pointer(arr)) // free the container; handles live on
	if n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]*Class, int(n))
	for i := range out {
		out[i] = newClass(slice[i])
	}
	runtime.KeepAlive(c)
	return out
}

// FindClass locates a class by its "Lpkg/Name;" descriptor, or nil.
func (c *Context) FindClass(descriptor string) *Class {
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	p := C.dexpp_context_find_class(c.ptr, cd)
	runtime.KeepAlive(c)
	if p == nil {
		return nil
	}
	return newClass(p)
}

// Strings returns the whole decoded string pool.
func (c *Context) Strings() []string {
	var n C.size_t
	arr := C.dexpp_context_strings(c.ptr, &n)
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
	runtime.KeepAlive(c)
	return out
}

// ---- Class --------------------------------------------------------------- //

// Class is a non-owning handle to a loaded class; it keeps the analysis alive.
type Class struct{ ptr *C.DexppClass }

func newClass(p *C.DexppClass) *Class {
	c := &Class{ptr: p}
	runtime.SetFinalizer(c, (*Class).Close)
	return c
}

// Close releases the class handle eagerly.
func (c *Class) Close() {
	if c.ptr != nil {
		C.dexpp_class_free(c.ptr)
		c.ptr = nil
		runtime.SetFinalizer(c, nil)
	}
}

func (c *Class) Name() string        { return takeString(C.dexpp_class_name(c.ptr)) }
func (c *Class) Package() string     { return takeString(C.dexpp_class_package(c.ptr)) }
func (c *Class) Superclass() string  { return takeString(C.dexpp_class_superclass(c.ptr)) }
func (c *Class) IsInterface() bool   { return C.dexpp_class_is_interface(c.ptr) != 0 }
func (c *Class) IsAbstract() bool    { return C.dexpp_class_is_abstract(c.ptr) != 0 }
func (c *Class) AccessFlags() uint32 { return uint32(C.dexpp_class_access_flags(c.ptr)) }

// Interfaces returns the descriptors of interfaces the class implements.
func (c *Class) Interfaces() []string {
	var n C.size_t
	arr := C.dexpp_class_interfaces(c.ptr, &n)
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
	runtime.KeepAlive(c)
	return out
}

// Methods returns the class's methods.
func (c *Class) Methods() []*Method {
	var n C.size_t
	arr := C.dexpp_class_methods(c.ptr, &n)
	defer C.free(unsafe.Pointer(arr))
	if n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]*Method, int(n))
	for i := range out {
		out[i] = newMethod(slice[i])
	}
	runtime.KeepAlive(c)
	return out
}

// ---- Method -------------------------------------------------------------- //

// Method is a non-owning handle to a method.
type Method struct{ ptr *C.DexppMethod }

func newMethod(p *C.DexppMethod) *Method {
	m := &Method{ptr: p}
	runtime.SetFinalizer(m, (*Method).Close)
	return m
}

// Close releases the method handle eagerly.
func (m *Method) Close() {
	if m.ptr != nil {
		C.dexpp_method_free(m.ptr)
		m.ptr = nil
		runtime.SetFinalizer(m, nil)
	}
}

func (m *Method) Name() string        { return takeString(C.dexpp_method_name(m.ptr)) }
func (m *Method) Descriptor() string  { return takeString(C.dexpp_method_descriptor(m.ptr)) }
func (m *Method) HasCode() bool       { return C.dexpp_method_has_code(m.ptr) != 0 }
func (m *Method) IsStatic() bool      { return C.dexpp_method_is_static(m.ptr) != 0 }
func (m *Method) AccessFlags() uint32 { return uint32(C.dexpp_method_access_flags(m.ptr)) }

// CallCount is the number of resolved invoke sites in the method.
func (m *Method) CallCount() int { return int(C.dexpp_method_call_count(m.ptr)) }

// ---- APK / manifest ------------------------------------------------------ //

// Apk is a package handle: archive + manifest + bridge to code analysis.
type Apk struct{ ptr *C.DexppApk }

// Component is a manifest component.
type Component struct {
	Name      string
	Kind      ComponentKind
	Exported  bool
	Reachable bool
}

// OpenAPK opens an APK file.
func OpenAPK(path string) (*Apk, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.char
	p := C.dexpp_apk_open(cpath, &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	a := &Apk{ptr: p}
	runtime.SetFinalizer(a, (*Apk).Close)
	return a, nil
}

// Close releases the APK handle eagerly.
func (a *Apk) Close() {
	if a.ptr != nil {
		C.dexpp_apk_free(a.ptr)
		a.ptr = nil
		runtime.SetFinalizer(a, nil)
	}
}

// Package is the manifest package name ("" if no decodable manifest).
func (a *Apk) Package() string {
	s := takeString(C.dexpp_apk_package(a.ptr))
	runtime.KeepAlive(a)
	return s
}

// Analysis builds a code-analysis context over the APK's classes*.dex.
func (a *Apk) Analysis() (*Context, error) {
	var cerr *C.char
	p := C.dexpp_apk_analysis(a.ptr, &cerr)
	runtime.KeepAlive(a)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

// Components returns the manifest's declared components.
func (a *Apk) Components() []Component {
	var n C.size_t
	arr := C.dexpp_apk_components(a.ptr, &n)
	if n == 0 {
		C.dexpp_component_array_free(arr, n)
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]Component, int(n))
	for i := range out {
		out[i] = Component{
			Name:      C.GoString(slice[i].name),
			Kind:      ComponentKind(slice[i].kind),
			Exported:  slice[i].exported != 0,
			Reachable: slice[i].reachable != 0,
		}
	}
	C.dexpp_component_array_free(arr, n)
	runtime.KeepAlive(a)
	return out
}
