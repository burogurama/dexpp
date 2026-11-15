// Package dexpp is a Go binding for dex++, a library for static analysis of
// Android APK and DEX files. It calls the library through its C API
// (bindings/c) with cgo.
//
// It reaches the same API as the Python binding:
//
//   - loading one DEX file, several DEX files, or every DEX file of an APK
//     (FromDEX, FromDEXFiles, FromAPK);
//   - classes, class references, methods and fields, with access flags;
//   - decoded bytecode, control-flow graphs, try blocks, annotations with
//     typed values, and the initial values of static fields;
//   - the calls, field accesses, string loads and type uses of a method, and
//     its parameter registers;
//   - the call graph, the class hierarchy and the cross-reference index;
//   - the APK: archive entries, the manifest, resources and signing (OpenAPK);
//   - the editable DEX model, a whole class at a time (ReadModel, NewModel).
//
// # Building
//
// The package links the dex++ static libraries, so build them first, from the
// root of the dex++ checkout:
//
//	cmake -S . -B build && cmake --build build --target dexpp_c -j
//
// This makes build/libdexpp_c.a and build/libdexpp_core.a. The cgo flags of
// this package link them by a path relative to this folder, so the build
// folder must be named build. Go programs also link the C++ runtime: libstdc++
// on Linux, libc++ on macOS. The package supports Linux and macOS. It does not
// support Windows.
//
// # Handles
//
// Context, Apk, Class, ClassRef, Method, Field, CallGraph, ClassHierarchy,
// Xrefs and Model are handles: each one owns an object on the C side. The
// garbage collector frees that object when the handle is no longer used.
// Close frees it at once, and calling Close again does nothing. Calling any
// other method on a closed handle, or on a nil handle, panics with a message
// that names the handle type.
//
// Every handle keeps the analysis it came from alive. So a Class stays usable
// after its Context is closed, and a Method stays usable after its Class is
// closed. Everything else, such as strings, slices and structs, is copied
// into Go memory and needs no cleanup.
//
// # Values that may be absent
//
// The package uses three forms, and never an empty string, for a value that
// may be absent:
//
//   - A method that returns a handle returns nil when there is none, for
//     example Context.FindClass and Class.Superclass.
//   - A method that returns a plain value returns a second result, a bool that
//     is false when there is none, for example Class.SourceFile,
//     Field.InitialValue and ClassHierarchy.SuperclassOf.
//   - A struct field that may be absent is a pointer, nil when absent, for
//     example Manifest.VersionName and ClassInfo.Superclass. The structs of
//     decoded code (instructions, control-flow edges, try blocks) are the
//     exception: they are many and small, so an optional number there comes
//     with a bool field, such as ReturnInstruction.HasReg.
//
// # Errors
//
// A call that fails returns an *Error. Its Code says what went wrong, and
// errors.Is compares an error with the sentinel value of each code:
//
//	if errors.Is(err, dexpp.ErrInvalidDex) { ... }
//
// errors.Is(err, fs.ErrNotExist) is true when a file does not exist. An
// *Error for an invalid model also lists every problem in Error.Problems.
//
// # Goroutines
//
// Reading from one analysis in several goroutines at once is safe: the lazy
// caches of the library are built under locks. Do not call Close on a handle
// while another goroutine still uses that handle. A Model can be read from
// several goroutines at once, but a change to it must not run at the same
// time as any other use of it.
package dexpp

/*
#cgo CFLAGS: -I${SRCDIR}/../../c -I${SRCDIR}/../../../src
#cgo LDFLAGS: ${SRCDIR}/../../../build/libdexpp_c.a ${SRCDIR}/../../../build/libdexpp_core.a
#cgo linux LDFLAGS: -lstdc++
#cgo darwin LDFLAGS: -lc++
#include <stdlib.h>
#include "dexpp.h"
#include "version.hpp"
*/
import "C"

import (
	"fmt"
	"io/fs"
	"runtime"
	"strings"
	"unsafe"
)

// Version is the version of the dex++ library that this package is built
// from, such as "0.2.0". It comes from src/version.hpp, the one place the
// version is written.
const Version = C.DEXPP_VERSION

// ---- Errors -------------------------------------------------------------- //

// ErrorCode says what went wrong in a failed call. The codes match the
// DexppErrorCode values of the C API and the exceptions of the Python binding.
type ErrorCode int

// The error codes.
const (
	// CodeFileNotFound: the path does not exist.
	CodeFileNotFound ErrorCode = C.DEXPP_ERROR_FILE_NOT_FOUND
	// CodeCannotRead: the path exists but cannot be read, for example because
	// it is a directory.
	CodeCannotRead ErrorCode = C.DEXPP_ERROR_CANNOT_READ
	// CodeCannotWrite: the file cannot be created or written.
	CodeCannotWrite ErrorCode = C.DEXPP_ERROR_CANNOT_WRITE
	// CodeInvalidAPK: the file is not a ZIP archive, or the APK has no
	// classes.dex.
	CodeInvalidAPK ErrorCode = C.DEXPP_ERROR_INVALID_APK
	// CodeInvalidDex: a DEX file cannot be parsed.
	CodeInvalidDex ErrorCode = C.DEXPP_ERROR_INVALID_DEX
	// CodeUnsupportedDex: a DEX format the model does not read, such as
	// version 41.
	CodeUnsupportedDex ErrorCode = C.DEXPP_ERROR_UNSUPPORTED_DEX
	// CodeInvalidModel: a model breaks rules of the DEX format. Error.Problems
	// lists them.
	CodeInvalidModel ErrorCode = C.DEXPP_ERROR_INVALID_MODEL
	// CodePoolOverflow: a table of the written file would have more entries
	// than the format allows.
	CodePoolOverflow ErrorCode = C.DEXPP_ERROR_POOL_OVERFLOW
	// CodeVersionTooLow: a model uses a feature that its DEX version does not
	// have.
	CodeVersionTooLow ErrorCode = C.DEXPP_ERROR_VERSION_TOO_LOW
	// CodeModel: a class to move or copy is missing from the source model, or
	// the destination model already has it.
	CodeModel ErrorCode = C.DEXPP_ERROR_MODEL
	// CodeInternal: an unexpected failure, such as running out of memory.
	CodeInternal ErrorCode = C.DEXPP_ERROR_INTERNAL
)

var codeNames = map[ErrorCode]string{
	CodeFileNotFound:   "file not found",
	CodeCannotRead:     "cannot read",
	CodeCannotWrite:    "cannot write",
	CodeInvalidAPK:     "invalid APK",
	CodeInvalidDex:     "invalid DEX",
	CodeUnsupportedDex: "unsupported DEX",
	CodeInvalidModel:   "invalid model",
	CodePoolOverflow:   "pool overflow",
	CodeVersionTooLow:  "version too low",
	CodeModel:          "model error",
	CodeInternal:       "internal error",
}

// String names the code, such as "invalid DEX".
func (c ErrorCode) String() string {
	if name, ok := codeNames[c]; ok {
		return name
	}
	return fmt.Sprintf("error code %d", int(c))
}

// Error is the error that every failed call returns.
type Error struct {
	// Code says what went wrong.
	Code ErrorCode
	// Message describes the failure, for example with the path of the file.
	Message string
	// Problems lists every rule of the DEX format that a model breaks. It is
	// set only for CodeInvalidModel.
	Problems []Problem
}

// Problem is one rule of the DEX format that a model breaks.
type Problem struct {
	// Where is the place of the problem, such as "Lcom/a/B;->run()V, item
	// 12". It is empty for a problem with the whole file.
	Where string
	// Message says what is wrong.
	Message string
}

// Error returns the message.
func (e *Error) Error() string { return e.Message }

// Is reports whether the error matches target, for errors.Is. An *Error
// matches every *Error with the same Code, so it matches the sentinel value of
// its code, such as ErrInvalidDex. An error with CodeFileNotFound also matches
// fs.ErrNotExist.
func (e *Error) Is(target error) bool {
	if t, ok := target.(*Error); ok {
		return t.Code == e.Code
	}
	return target == fs.ErrNotExist && e.Code == CodeFileNotFound
}

// The sentinel errors, one per code. Compare an error with them through
// errors.Is: errors.Is(err, ErrInvalidDex) is true for every *Error with
// CodeInvalidDex. Do not compare with ==, because a failed call returns a new
// *Error.
var (
	ErrFileNotFound   = &Error{Code: CodeFileNotFound, Message: "file not found"}
	ErrCannotRead     = &Error{Code: CodeCannotRead, Message: "cannot read"}
	ErrCannotWrite    = &Error{Code: CodeCannotWrite, Message: "cannot write"}
	ErrInvalidAPK     = &Error{Code: CodeInvalidAPK, Message: "invalid APK"}
	ErrInvalidDex     = &Error{Code: CodeInvalidDex, Message: "invalid DEX"}
	ErrUnsupportedDex = &Error{Code: CodeUnsupportedDex, Message: "unsupported DEX"}
	ErrInvalidModel   = &Error{Code: CodeInvalidModel, Message: "invalid model"}
	ErrPoolOverflow   = &Error{Code: CodePoolOverflow, Message: "pool overflow"}
	ErrVersionTooLow  = &Error{Code: CodeVersionTooLow, Message: "version too low"}
	ErrModel          = &Error{Code: CodeModel, Message: "model error"}
	ErrInternal       = &Error{Code: CodeInternal, Message: "internal error"}
)

// goError turns a C error into an *Error and frees the C object.
func goError(cerr *C.DexppError) error {
	if cerr == nil {
		return &Error{Code: CodeInternal, Message: "dexpp: the call failed without an error"}
	}
	defer C.dexpp_error_free(cerr)
	e := &Error{
		Code:    ErrorCode(C.dexpp_error_code(cerr)),
		Message: C.GoString(C.dexpp_error_message(cerr)),
	}
	n := int(C.dexpp_error_problem_count(cerr))
	if n > 0 {
		e.Problems = make([]Problem, n)
		for i := range e.Problems {
			e.Problems[i] = Problem{
				Where:   C.GoString(C.dexpp_error_problem_where(cerr, C.size_t(i))),
				Message: C.GoString(C.dexpp_error_problem_message(cerr, C.size_t(i))),
			}
		}
	}
	return e
}

// panicNil and panicClosed report a call on a handle that cannot reach C.
func panicNil(kind string) {
	panic("dexpp: method called on a nil *" + kind)
}

func panicClosed(kind string) {
	panic("dexpp: *" + kind + " used after Close")
}

// ---- Strings and arrays -------------------------------------------------- //

// takeString copies a malloc'd C string into Go and frees it.
func takeString(s *C.char) string {
	if s == nil {
		return ""
	}
	defer C.dexpp_string_free(s)
	return C.GoString(s)
}

// takeOptString is takeString for a string that may be absent (NULL).
func takeOptString(s *C.char) (string, bool) {
	if s == nil {
		return "", false
	}
	return takeString(s), true
}

// goOptString copies a C string that a struct owns; nil stays nil.
func goOptString(s *C.char) *string {
	if s == nil {
		return nil
	}
	out := C.GoString(s)
	return &out
}

// goBytes copies n bytes of C memory into a new Go slice.
func goBytes(p unsafe.Pointer, n C.size_t) []byte {
	if p == nil || n == 0 {
		return nil
	}
	out := make([]byte, int(n))
	copy(out, unsafe.Slice((*byte)(p), int(n)))
	return out
}

// goText copies a DexppText, NUL bytes included.
func goText(t C.DexppText) string {
	if t.data == nil || t.length == 0 {
		return ""
	}
	return string(unsafe.Slice((*byte)(unsafe.Pointer(t.data)), int(t.length)))
}

// goStrings copies an array of n C strings that a struct owns.
func goStrings(arr **C.char, n C.size_t) []string {
	if arr == nil || n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]string, int(n))
	for i := range out {
		out[i] = C.GoString(slice[i])
	}
	return out
}

// takeStringArray copies a malloc'd array of n C strings into Go and frees
// the whole array.
func takeStringArray(arr **C.char, n C.size_t) []string {
	defer C.dexpp_string_array_free(arr, n)
	return goStrings(arr, n)
}

// takeTextArray copies a DexppText array into Go and frees it.
func takeTextArray(arr *C.DexppText, n C.size_t) []string {
	defer C.dexpp_text_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]string, int(n))
	for i := range out {
		out[i] = goText(slice[i])
	}
	return out
}

// goIndices copies an array of n indices that a struct owns.
func goIndices(arr *C.uint32_t, n C.size_t) []uint32 {
	if arr == nil || n == 0 {
		return nil
	}
	slice := unsafe.Slice(arr, int(n))
	out := make([]uint32, int(n))
	for i := range out {
		out[i] = uint32(slice[i])
	}
	return out
}

// takeIndexArray copies a uint32 index array into Go and frees it.
func takeIndexArray(arr *C.uint32_t, n C.size_t) []uint32 {
	defer C.dexpp_index_array_free(arr, n)
	return goIndices(arr, n)
}

// cStrings copies Go strings into C memory. The caller frees them with
// freeCStrings.
func cStrings(items []string) []*C.char {
	out := make([]*C.char, len(items))
	for i, s := range items {
		out[i] = C.CString(s)
	}
	return out
}

func freeCStrings(items []*C.char) {
	for _, p := range items {
		C.free(unsafe.Pointer(p))
	}
}

func cbool(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

// ---- Access flags -------------------------------------------------------- //

// AccessFlags are the access flags of a class, a method or a field, as the
// DEX format stores them. Two pairs of flags share a bit: AccessVolatile and
// AccessBridge, and AccessTransient and AccessVarargs. The first of each pair
// is for fields, the second for methods.
type AccessFlags uint32

// The access flags.
const (
	AccessPublic               AccessFlags = C.DEXPP_ACC_PUBLIC
	AccessPrivate              AccessFlags = C.DEXPP_ACC_PRIVATE
	AccessProtected            AccessFlags = C.DEXPP_ACC_PROTECTED
	AccessStatic               AccessFlags = C.DEXPP_ACC_STATIC
	AccessFinal                AccessFlags = C.DEXPP_ACC_FINAL
	AccessSynchronized         AccessFlags = C.DEXPP_ACC_SYNCHRONIZED
	AccessVolatile             AccessFlags = C.DEXPP_ACC_VOLATILE  // fields; the same bit as AccessBridge
	AccessBridge               AccessFlags = C.DEXPP_ACC_BRIDGE    // methods; the same bit as AccessVolatile
	AccessTransient            AccessFlags = C.DEXPP_ACC_TRANSIENT // fields; the same bit as AccessVarargs
	AccessVarargs              AccessFlags = C.DEXPP_ACC_VARARGS   // methods; the same bit as AccessTransient
	AccessNative               AccessFlags = C.DEXPP_ACC_NATIVE
	AccessInterface            AccessFlags = C.DEXPP_ACC_INTERFACE
	AccessAbstract             AccessFlags = C.DEXPP_ACC_ABSTRACT
	AccessStrict               AccessFlags = C.DEXPP_ACC_STRICT
	AccessSynthetic            AccessFlags = C.DEXPP_ACC_SYNTHETIC
	AccessAnnotation           AccessFlags = C.DEXPP_ACC_ANNOTATION
	AccessEnum                 AccessFlags = C.DEXPP_ACC_ENUM
	AccessConstructor          AccessFlags = C.DEXPP_ACC_CONSTRUCTOR
	AccessDeclaredSynchronized AccessFlags = C.DEXPP_ACC_DECLARED_SYNCHRONIZED
)

// Has reports whether every bit of flag is set.
func (f AccessFlags) Has(flag AccessFlags) bool { return f&flag == flag }

var accessNames = []struct {
	flag AccessFlags
	name string
}{
	{AccessPublic, "public"}, {AccessPrivate, "private"}, {AccessProtected, "protected"},
	{AccessStatic, "static"}, {AccessFinal, "final"}, {AccessSynchronized, "synchronized"},
	{AccessVolatile, "volatile/bridge"}, {AccessTransient, "transient/varargs"},
	{AccessNative, "native"}, {AccessInterface, "interface"}, {AccessAbstract, "abstract"},
	{AccessStrict, "strict"}, {AccessSynthetic, "synthetic"}, {AccessAnnotation, "annotation"},
	{AccessEnum, "enum"}, {AccessConstructor, "constructor"},
	{AccessDeclaredSynchronized, "declared-synchronized"},
}

// String lists the flags that are set, such as "public|static". A bit that
// two flags share is written as both names, such as "volatile/bridge". It
// returns "0" when no flag is set.
func (f AccessFlags) String() string {
	var names []string
	rest := f
	for _, a := range accessNames {
		if f&a.flag != 0 {
			names = append(names, a.name)
			rest &^= a.flag
		}
	}
	if rest != 0 {
		names = append(names, fmt.Sprintf("0x%x", uint32(rest)))
	}
	if len(names) == 0 {
		return "0"
	}
	return strings.Join(names, "|")
}

// ---- Enums --------------------------------------------------------------- //

// InvokeKind is the kind of invoke instruction at a call site.
type InvokeKind int

// The invoke kinds. AnyInvokeKind is not a kind: it is the filter value that
// matches every kind in CallGraph.EdgesWhere.
const (
	InvokeVirtual     InvokeKind = C.DEXPP_INVOKE_VIRTUAL     // invoke-virtual
	InvokeSuper       InvokeKind = C.DEXPP_INVOKE_SUPER       // invoke-super
	InvokeDirect      InvokeKind = C.DEXPP_INVOKE_DIRECT      // invoke-direct: constructors and private methods
	InvokeStatic      InvokeKind = C.DEXPP_INVOKE_STATIC      // invoke-static
	InvokeInterface   InvokeKind = C.DEXPP_INVOKE_INTERFACE   // invoke-interface
	InvokePolymorphic InvokeKind = C.DEXPP_INVOKE_POLYMORPHIC // invoke-polymorphic: a call through a method handle
	InvokeCustom      InvokeKind = C.DEXPP_INVOKE_CUSTOM      // invoke-custom: a call site bound at runtime
	AnyInvokeKind     InvokeKind = -1
)

// String names the kind, such as "virtual".
func (k InvokeKind) String() string {
	if k == AnyInvokeKind {
		return "any"
	}
	return enumName(int(k), "virtual", "super", "direct", "static", "interface", "polymorphic",
		"custom")
}

// EdgeOrigin is how a call-graph edge came to point at its callee.
type EdgeOrigin int

// The edge origins. AnyEdgeOrigin is not an origin: it is the filter value
// that matches both origins in CallGraph.EdgesWhere.
const (
	// EdgeDeclared: the callee is the target the bytecode names. Every call
	// site has exactly one such edge.
	EdgeDeclared EdgeOrigin = C.DEXPP_EDGE_DECLARED
	// EdgeInheritedResolution: the class the bytecode names does not define
	// the method, and the callee is the nearest definition in a loaded
	// superclass or interface. A call site has at most one such edge.
	EdgeInheritedResolution EdgeOrigin = C.DEXPP_EDGE_INHERITED_RESOLUTION
	AnyEdgeOrigin           EdgeOrigin = -1
)

// String names the origin, such as "declared".
func (o EdgeOrigin) String() string {
	if o == AnyEdgeOrigin {
		return "any"
	}
	return enumName(int(o), "declared", "inherited-resolution")
}

// CfgEdgeKind is the kind of an edge between two basic blocks.
type CfgEdgeKind int

// The kinds of control-flow edge.
const (
	CfgFallthrough    CfgEdgeKind = C.DEXPP_CFG_FALLTHROUGH      // execution runs on into the next block
	CfgGoto           CfgEdgeKind = C.DEXPP_CFG_GOTO             // an unconditional goto
	CfgBranchTaken    CfgEdgeKind = C.DEXPP_CFG_BRANCH_TAKEN     // the condition of an if holds
	CfgBranchNotTaken CfgEdgeKind = C.DEXPP_CFG_BRANCH_NOT_TAKEN // the condition of an if does not hold
	CfgSwitchCase     CfgEdgeKind = C.DEXPP_CFG_SWITCH_CASE      // one case of a switch; the label is the key
	CfgSwitchDefault  CfgEdgeKind = C.DEXPP_CFG_SWITCH_DEFAULT   // no case of a switch matches
	CfgException      CfgEdgeKind = C.DEXPP_CFG_EXCEPTION        // to a catch handler; the label is the type index
)

// String names the kind, such as "branch-taken".
func (k CfgEdgeKind) String() string {
	return enumName(int(k), "fallthrough", "goto", "branch-taken", "branch-not-taken",
		"switch-case", "switch-default", "exception")
}

// TypeUseKind is what an instruction does with the type it names.
type TypeUseKind int

// The type reference kinds.
const (
	TypeUseConstClass     TypeUseKind = C.DEXPP_TYPE_USE_CONST_CLASS      // const-class: loads the class object
	TypeUseCheckCast      TypeUseKind = C.DEXPP_TYPE_USE_CHECK_CAST       // check-cast: a cast to the type
	TypeUseInstanceOf     TypeUseKind = C.DEXPP_TYPE_USE_INSTANCE_OF      // instance-of: an instanceof test
	TypeUseNewInstance    TypeUseKind = C.DEXPP_TYPE_USE_NEW_INSTANCE     // new-instance: creates an object
	TypeUseNewArray       TypeUseKind = C.DEXPP_TYPE_USE_NEW_ARRAY        // new-array; the type is the array type
	TypeUseFilledNewArray TypeUseKind = C.DEXPP_TYPE_USE_FILLED_NEW_ARRAY // filled-new-array; the type is the array type
)

// String names the kind, such as "new-instance".
func (k TypeUseKind) String() string {
	return enumName(int(k), "const-class", "check-cast", "instance-of", "new-instance",
		"new-array", "filled-new-array")
}

// OperandWidth is how many registers a value takes.
type OperandWidth int

// The operand widths.
const (
	WidthSingle OperandWidth = C.DEXPP_WIDTH_SINGLE // a 32-bit value or a reference: one register
	WidthPair   OperandWidth = C.DEXPP_WIDTH_PAIR   // long or double: two registers, vN and vN+1
)

// String names the width: "single" or "pair".
func (w OperandWidth) String() string { return enumName(int(w), "single", "pair") }

// AnnotationVisibility is who can see an annotation.
type AnnotationVisibility int

// The annotation visibilities.
const (
	// VisibilityBuild: only tools that read the file; the running app does
	// not see the annotation.
	VisibilityBuild AnnotationVisibility = C.DEXPP_VISIBILITY_BUILD
	// VisibilityRuntime: the running app can read the annotation through
	// reflection.
	VisibilityRuntime AnnotationVisibility = C.DEXPP_VISIBILITY_RUNTIME
	// VisibilitySystem: data the compiler writes for the runtime, in the
	// dalvik.annotation.* annotations.
	VisibilitySystem AnnotationVisibility = C.DEXPP_VISIBILITY_SYSTEM
)

// String names the visibility, such as "runtime".
func (v AnnotationVisibility) String() string {
	return enumName(int(v), "build", "runtime", "system")
}

// ComponentKind is the kind of a component declared in the manifest.
type ComponentKind int

// The component kinds.
const (
	ComponentActivity ComponentKind = C.DEXPP_COMPONENT_ACTIVITY // an <activity>: a screen of the app
	ComponentService  ComponentKind = C.DEXPP_COMPONENT_SERVICE  // a <service>: work without a screen
	ComponentReceiver ComponentKind = C.DEXPP_COMPONENT_RECEIVER // a <receiver>: a broadcast receiver
	ComponentProvider ComponentKind = C.DEXPP_COMPONENT_PROVIDER // a <provider>: a content provider
)

// String names the kind, such as "activity".
func (k ComponentKind) String() string {
	return enumName(int(k), "activity", "service", "receiver", "provider")
}

func enumName(v int, names ...string) string {
	if v >= 0 && v < len(names) {
		return names[v]
	}
	return fmt.Sprintf("unknown(%d)", v)
}

// ---- Identities ---------------------------------------------------------- //

// MethodID is the identity of a method: its class, its name and its
// prototype. It is a plain value, so it can be compared with == and used as
// a map key. An identity also exists for a method that is not in the loaded
// files, such as a framework method.
type MethodID struct {
	ClassDescriptor string // the class, such as "Ljava/lang/Object;"
	Name            string // the method name, such as "toString"; a constructor is "<init>"
	Proto           string // the prototype, such as "()Ljava/lang/String;"
}

// String writes the identity as "Lcls;->name(proto)", such as
// "Ljava/lang/Object;->toString()Ljava/lang/String;".
func (id MethodID) String() string { return id.ClassDescriptor + "->" + id.Name + id.Proto }

// FieldID is the identity of a field: its class, its name and its type. It
// is a plain value, so it can be compared with == and used as a map key.
type FieldID struct {
	ClassDescriptor string // the class, such as "Ljava/lang/System;"
	Name            string // the field name, such as "out"
	Type            string // the type descriptor, such as "Ljava/io/PrintStream;"
}

// String writes the identity as "Lcls;->name:type", such as
// "Ljava/lang/System;->out:Ljava/io/PrintStream;".
func (id FieldID) String() string { return id.ClassDescriptor + "->" + id.Name + ":" + id.Type }

func goMethodID(id C.DexppMethodId) MethodID {
	return MethodID{C.GoString(id.class_descriptor), C.GoString(id.name), C.GoString(id.proto)}
}

func goFieldID(id C.DexppFieldId) FieldID {
	return FieldID{C.GoString(id.class_descriptor), C.GoString(id.name), C.GoString(id._type)}
}

// ---- Context ------------------------------------------------------------- //

// Context is the code of a program: one or more DEX files loaded together. It
// is the starting point for code analysis. Loading does little work: class
// data, bytecode and the whole-program structures are built the first time
// they are needed, and kept.
type Context struct{ ptr *C.DexppContext }

// FromDEX loads one DEX file. It fails with CodeFileNotFound, CodeCannotRead
// or CodeInvalidDex.
func FromDEX(path string) (*Context, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.DexppError
	p := C.dexpp_context_from_dex(cpath, &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

// FromDEXFiles loads several DEX files into one context, for example the
// files of a multidex app that are already unpacked. It fails as FromDEX does,
// for the first file that cannot be loaded.
func FromDEXFiles(paths []string) (*Context, error) {
	cpaths := cStrings(paths)
	defer freeCStrings(cpaths)
	// cgo allows passing &cpaths[0]: the slice holds C pointers, not Go
	// pointers.
	var first **C.char
	if len(cpaths) > 0 {
		first = &cpaths[0]
	}
	var cerr *C.DexppError
	p := C.dexpp_context_from_dex_files(first, C.size_t(len(cpaths)), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

// LoadOption changes how FromAPK loads an APK.
type LoadOption func(*loadOptions)

type loadOptions struct{ threads uint32 }

// WithThreads sets how many threads FromAPK uses to decompress the DEX files
// and, later, to build the call graph. 1 keeps all work on the calling
// thread; use it when the program already analyses several APKs in parallel.
// 0, the default, lets the library choose. A negative number counts as 0.
func WithThreads(n int) LoadOption {
	return func(o *loadOptions) {
		if n < 0 {
			n = 0
		}
		o.threads = uint32(n)
	}
}

// FromAPK loads every top-level classes*.dex entry of an APK, in multidex
// order: classes.dex, classes2.dex, classes3.dex and so on. It fails with
// CodeFileNotFound, CodeCannotRead, CodeInvalidAPK or CodeInvalidDex.
func FromAPK(path string, opts ...LoadOption) (*Context, error) {
	var o loadOptions
	for _, opt := range opts {
		if opt != nil {
			opt(&o)
		}
	}
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.DexppError
	p := C.dexpp_context_from_apk(cpath, C.uint32_t(o.threads), &cerr)
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

// Close frees the context at once. Handles that came from it stay valid.
// Calling Close again, or on a nil *Context, does nothing.
func (c *Context) Close() {
	if c == nil || c.ptr == nil {
		return
	}
	C.dexpp_context_free(c.ptr)
	c.ptr = nil
	runtime.SetFinalizer(c, nil)
}

func (c *Context) p() *C.DexppContext {
	if c == nil {
		panicNil("Context")
	}
	if c.ptr == nil {
		panicClosed("Context")
	}
	return c.ptr
}

// Classes returns every class of every loaded DEX file, in file order.
func (c *Context) Classes() []*Class {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_context_classes(c.p(), &n)
	return takeClasses(arr, n)
}

// FindClass returns the class with this descriptor, such as
// "Lcom/example/Foo;", or nil when no loaded file defines it.
func (c *Context) FindClass(descriptor string) *Class {
	defer runtime.KeepAlive(c)
	cd := C.CString(descriptor)
	defer C.free(unsafe.Pointer(cd))
	p := C.dexpp_context_find_class(c.p(), cd)
	if p == nil {
		return nil
	}
	return newClass(p)
}

// Strings returns every string of every loaded DEX file's string table. This
// includes the names of classes, methods and fields, not only the strings the
// code loads, and a string that is in two DEX files appears twice. It decodes
// the whole table, so it is one of the more expensive calls. For only the
// strings the code loads, use Xrefs.ReferencedStrings.
func (c *Context) Strings() []string {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_context_strings(c.p(), &n)
	return takeTextArray(arr, n)
}

// CallGraph returns the call graph of the whole program. It is built on the
// first call and kept, so later calls are cheap. It fails only with
// CodeInternal.
func (c *Context) CallGraph() (*CallGraph, error) {
	defer runtime.KeepAlive(c)
	var cerr *C.DexppError
	p := C.dexpp_context_call_graph(c.p(), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	g := &CallGraph{ptr: p}
	runtime.SetFinalizer(g, (*CallGraph).Close)
	return g, nil
}

// ClassHierarchy returns the class hierarchy of the whole program. It is
// built on the first call and kept. It fails only with CodeInternal.
func (c *Context) ClassHierarchy() (*ClassHierarchy, error) {
	defer runtime.KeepAlive(c)
	var cerr *C.DexppError
	p := C.dexpp_context_class_hierarchy(c.p(), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	h := &ClassHierarchy{ptr: p}
	runtime.SetFinalizer(h, (*ClassHierarchy).Close)
	return h, nil
}

// Xrefs returns the cross-reference index of the whole program. It is built
// on the first call and kept. It fails only with CodeInternal.
func (c *Context) Xrefs() (*Xrefs, error) {
	defer runtime.KeepAlive(c)
	var cerr *C.DexppError
	p := C.dexpp_context_xrefs(c.p(), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	x := &Xrefs{ptr: p}
	runtime.SetFinalizer(x, (*Xrefs).Close)
	return x, nil
}
