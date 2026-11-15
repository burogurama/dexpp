package dexpp

// Classes, class references, methods, fields, and annotations with their
// values.

/*
#include <stdlib.h>
#include "dexpp.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// ---- Class --------------------------------------------------------------- //

// Class is a class defined in a loaded DEX file. It is a handle: it keeps the
// analysis alive (see the package documentation).
type Class struct{ ptr *C.DexppClass }

func newClass(p *C.DexppClass) *Class {
	c := &Class{ptr: p}
	runtime.SetFinalizer(c, (*Class).Close)
	return c
}

// takeClasses wraps every handle of a C class array and frees the array
// itself. The C header allows this: the array is one block from malloc(), so
// a caller that keeps the handles frees the block alone with free().
func takeClasses(arr **C.DexppClass, n C.size_t) []*Class {
	if arr == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(arr))
	out := make([]*Class, int(n))
	for i, p := range unsafe.Slice(arr, int(n)) {
		out[i] = newClass(p)
	}
	return out
}

// Close frees the class handle at once. Calling Close again, or on a nil
// *Class, does nothing.
func (c *Class) Close() {
	if c == nil || c.ptr == nil {
		return
	}
	C.dexpp_class_free(c.ptr)
	c.ptr = nil
	runtime.SetFinalizer(c, nil)
}

func (c *Class) p() *C.DexppClass {
	if c == nil {
		panicNil("Class")
	}
	if c.ptr == nil {
		panicClosed("Class")
	}
	return c.ptr
}

// Descriptor returns the class descriptor, such as "Lcom/example/Foo;".
// Method.Name and Field.Name are simple names; a class is named by its
// descriptor.
func (c *Class) Descriptor() string {
	defer runtime.KeepAlive(c)
	return takeString(C.dexpp_class_descriptor(c.p()))
}

// Package returns the package with "/" between its parts, such as
// "com/example". It is empty for a class in the default package.
func (c *Class) Package() string {
	defer runtime.KeepAlive(c)
	return takeString(C.dexpp_class_package(c.p()))
}

// Superclass returns the direct superclass. It is nil only for
// java.lang.Object itself, which has no superclass.
func (c *Class) Superclass() *ClassRef {
	defer runtime.KeepAlive(c)
	return newClassRef(C.dexpp_class_superclass(c.p()))
}

// Interfaces returns the interfaces the class implements directly, in
// declaration order. For an interface, these are the interfaces it extends.
func (c *Class) Interfaces() []*ClassRef {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_class_interfaces(c.p(), &n)
	return takeClassRefs(arr, n)
}

// AccessFlags returns the class's access flags.
func (c *Class) AccessFlags() AccessFlags {
	defer runtime.KeepAlive(c)
	return AccessFlags(C.dexpp_class_access_flags(c.p()))
}

// IsInterface reports whether the class is an interface.
func (c *Class) IsInterface() bool {
	defer runtime.KeepAlive(c)
	return C.dexpp_class_is_interface(c.p()) != 0
}

// IsAbstract reports whether the class is abstract. Interfaces are abstract
// too.
func (c *Class) IsAbstract() bool {
	defer runtime.KeepAlive(c)
	return C.dexpp_class_is_abstract(c.p()) != 0
}

// IsPublic reports whether the class is public.
func (c *Class) IsPublic() bool { return c.AccessFlags().Has(AccessPublic) }

// IsFinal reports whether the class is final.
func (c *Class) IsFinal() bool { return c.AccessFlags().Has(AccessFinal) }

// IsEnum reports whether the class is an enum.
func (c *Class) IsEnum() bool { return c.AccessFlags().Has(AccessEnum) }

// SourceFile returns the source file name recorded for the class, such as
// "Foo.java". The bool is false when the DEX file records none.
func (c *Class) SourceFile() (string, bool) {
	defer runtime.KeepAlive(c)
	return takeOptString(C.dexpp_class_source_file(c.p()))
}

// Methods returns the methods the class defines: first the direct methods
// (constructors, static and private methods), then the virtual methods.
// Inherited methods are not included.
func (c *Class) Methods() []*Method {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_class_methods(c.p(), &n)
	if arr == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(arr)) // the handles live on in Go
	out := make([]*Method, int(n))
	for i, p := range unsafe.Slice(arr, int(n)) {
		out[i] = newMethod(p)
	}
	return out
}

// Fields returns the fields the class declares: first the static fields, then
// the instance fields. Inherited fields are not included.
func (c *Class) Fields() []*Field {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_class_fields(c.p(), &n)
	if arr == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(arr)) // the handles live on in Go
	out := make([]*Field, int(n))
	for i, p := range unsafe.Slice(arr, int(n)) {
		out[i] = newField(p)
	}
	return out
}

// Annotations returns the annotations on the class.
func (c *Class) Annotations() []Annotation {
	defer runtime.KeepAlive(c)
	var n C.size_t
	arr := C.dexpp_class_annotations(c.p(), &n)
	return takeAnnotations(arr, n)
}

// ---- ClassRef ------------------------------------------------------------ //

// ClassRef is a reference to a class by its descriptor. The class may not be
// in the loaded files: superclasses and interfaces are often framework
// classes, which an app does not contain. It is a handle (see the package
// documentation).
type ClassRef struct{ ptr *C.DexppClassRef }

// newClassRef wraps a C class reference; NULL becomes nil.
func newClassRef(p *C.DexppClassRef) *ClassRef {
	if p == nil {
		return nil
	}
	r := &ClassRef{ptr: p}
	runtime.SetFinalizer(r, (*ClassRef).Close)
	return r
}

// takeClassRefs wraps every handle of a C class reference array and frees the
// array itself, as takeClasses does.
func takeClassRefs(arr **C.DexppClassRef, n C.size_t) []*ClassRef {
	if arr == nil {
		return nil
	}
	defer C.free(unsafe.Pointer(arr))
	out := make([]*ClassRef, int(n))
	for i, p := range unsafe.Slice(arr, int(n)) {
		out[i] = newClassRef(p)
	}
	return out
}

// Close frees the reference at once. Calling Close again, or on a nil
// *ClassRef, does nothing.
func (r *ClassRef) Close() {
	if r == nil || r.ptr == nil {
		return
	}
	C.dexpp_class_ref_free(r.ptr)
	r.ptr = nil
	runtime.SetFinalizer(r, nil)
}

func (r *ClassRef) p() *C.DexppClassRef {
	if r == nil {
		panicNil("ClassRef")
	}
	if r.ptr == nil {
		panicClosed("ClassRef")
	}
	return r.ptr
}

// Descriptor returns the descriptor the reference names, such as
// "Ljava/lang/Object;".
func (r *ClassRef) Descriptor() string {
	defer runtime.KeepAlive(r)
	return takeString(C.dexpp_class_ref_descriptor(r.p()))
}

// IsResolved reports whether the class is defined in a loaded file.
func (r *ClassRef) IsResolved() bool {
	defer runtime.KeepAlive(r)
	return C.dexpp_class_ref_is_resolved(r.p()) != 0
}

// Resolve returns the referenced class, or nil when no loaded file defines
// it.
func (r *ClassRef) Resolve() *Class {
	defer runtime.KeepAlive(r)
	p := C.dexpp_class_ref_resolve(r.p())
	if p == nil {
		return nil
	}
	return newClass(p)
}

// ---- Method -------------------------------------------------------------- //

// Method is a method defined by a class in a loaded DEX file. It is a handle
// (see the package documentation). Its code is in code.go: Instructions, Cfg,
// TryBlocks, Calls and the other methods that read the bytecode.
type Method struct{ ptr *C.DexppMethod }

func newMethod(p *C.DexppMethod) *Method {
	m := &Method{ptr: p}
	runtime.SetFinalizer(m, (*Method).Close)
	return m
}

// Close frees the method handle at once. Calling Close again, or on a nil
// *Method, does nothing.
func (m *Method) Close() {
	if m == nil || m.ptr == nil {
		return
	}
	C.dexpp_method_free(m.ptr)
	m.ptr = nil
	runtime.SetFinalizer(m, nil)
}

func (m *Method) p() *C.DexppMethod {
	if m == nil {
		panicNil("Method")
	}
	if m.ptr == nil {
		panicClosed("Method")
	}
	return m.ptr
}

// Name returns the method name. A constructor is "<init>", and the static
// initializer is "<clinit>".
func (m *Method) Name() string {
	defer runtime.KeepAlive(m)
	return takeString(C.dexpp_method_name(m.p()))
}

// Descriptor returns the prototype: the parameter types inside parentheses,
// then the return type, such as "(Ljava/lang/String;I)V".
func (m *Method) Descriptor() string {
	defer runtime.KeepAlive(m)
	return takeString(C.dexpp_method_descriptor(m.p()))
}

// DeclaringClass returns the class that defines the method.
func (m *Method) DeclaringClass() *ClassRef {
	defer runtime.KeepAlive(m)
	return newClassRef(C.dexpp_method_declaring_class(m.p()))
}

// ID returns the method's identity: its class, name and prototype. It is the
// identity that the call graph and the cross-references use.
func (m *Method) ID() MethodID {
	ref := m.DeclaringClass()
	defer ref.Close()
	return MethodID{ClassDescriptor: ref.Descriptor(), Name: m.Name(), Proto: m.Descriptor()}
}

// ReturnType returns the descriptor of the return type; "V" means void.
func (m *Method) ReturnType() string {
	defer runtime.KeepAlive(m)
	return takeString(C.dexpp_method_return_type(m.p()))
}

// Parameters returns the descriptors of the declared parameter types, in
// order. The implicit "this" is not included.
func (m *Method) Parameters() []string {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_parameters(m.p(), &n)
	return takeStringArray(arr, n)
}

// AccessFlags returns the method's access flags.
func (m *Method) AccessFlags() AccessFlags {
	defer runtime.KeepAlive(m)
	return AccessFlags(C.dexpp_method_access_flags(m.p()))
}

// HasCode reports whether the method has bytecode. It is false for abstract
// and native methods.
func (m *Method) HasCode() bool {
	defer runtime.KeepAlive(m)
	return C.dexpp_method_has_code(m.p()) != 0
}

// IsStatic reports whether the method is static.
func (m *Method) IsStatic() bool {
	defer runtime.KeepAlive(m)
	return C.dexpp_method_is_static(m.p()) != 0
}

// IsConstructor reports whether the method is "<init>" or "<clinit>".
func (m *Method) IsConstructor() bool {
	defer runtime.KeepAlive(m)
	return C.dexpp_method_is_constructor(m.p()) != 0
}

// IsPublic reports whether the method is public.
func (m *Method) IsPublic() bool { return m.AccessFlags().Has(AccessPublic) }

// IsPrivate reports whether the method is private.
func (m *Method) IsPrivate() bool { return m.AccessFlags().Has(AccessPrivate) }

// IsAbstract reports whether the method is abstract.
func (m *Method) IsAbstract() bool { return m.AccessFlags().Has(AccessAbstract) }

// IsNative reports whether the method is native: its code is not in the DEX
// file.
func (m *Method) IsNative() bool { return m.AccessFlags().Has(AccessNative) }

// Annotations returns the annotations on the method.
func (m *Method) Annotations() []Annotation {
	defer runtime.KeepAlive(m)
	var n C.size_t
	arr := C.dexpp_method_annotations(m.p(), &n)
	return takeAnnotations(arr, n)
}

// ---- Field --------------------------------------------------------------- //

// Field is a field declared by a class in a loaded DEX file. It is a handle
// (see the package documentation).
type Field struct{ ptr *C.DexppField }

func newField(p *C.DexppField) *Field {
	f := &Field{ptr: p}
	runtime.SetFinalizer(f, (*Field).Close)
	return f
}

// Close frees the field handle at once. Calling Close again, or on a nil
// *Field, does nothing.
func (f *Field) Close() {
	if f == nil || f.ptr == nil {
		return
	}
	C.dexpp_field_free(f.ptr)
	f.ptr = nil
	runtime.SetFinalizer(f, nil)
}

func (f *Field) p() *C.DexppField {
	if f == nil {
		panicNil("Field")
	}
	if f.ptr == nil {
		panicClosed("Field")
	}
	return f.ptr
}

// Name returns the field name.
func (f *Field) Name() string {
	defer runtime.KeepAlive(f)
	return takeString(C.dexpp_field_name(f.p()))
}

// DeclaringClass returns the class that declares the field.
func (f *Field) DeclaringClass() *ClassRef {
	defer runtime.KeepAlive(f)
	return newClassRef(C.dexpp_field_declaring_class(f.p()))
}

// Type returns the descriptor of the field's type.
func (f *Field) Type() string {
	defer runtime.KeepAlive(f)
	return takeString(C.dexpp_field_type(f.p()))
}

// ID returns the field's identity: its class, name and type. It is the
// identity that the cross-references use.
func (f *Field) ID() FieldID {
	ref := f.DeclaringClass()
	defer ref.Close()
	return FieldID{ClassDescriptor: ref.Descriptor(), Name: f.Name(), Type: f.Type()}
}

// AccessFlags returns the field's access flags.
func (f *Field) AccessFlags() AccessFlags {
	defer runtime.KeepAlive(f)
	return AccessFlags(C.dexpp_field_access_flags(f.p()))
}

// IsPublic reports whether the field is public.
func (f *Field) IsPublic() bool { return f.AccessFlags().Has(AccessPublic) }

// IsStatic reports whether the field is static.
func (f *Field) IsStatic() bool { return f.AccessFlags().Has(AccessStatic) }

// IsFinal reports whether the field is final.
func (f *Field) IsFinal() bool { return f.AccessFlags().Has(AccessFinal) }

// Annotations returns the annotations on the field.
func (f *Field) Annotations() []Annotation {
	defer runtime.KeepAlive(f)
	var n C.size_t
	arr := C.dexpp_field_annotations(f.p(), &n)
	return takeAnnotations(arr, n)
}

// InitialValue returns the constant a static field starts with. The bool is
// false when the field is not static or the DEX file records no value for it;
// the field then starts at the default of its type: zero, false or null. The
// value has one of the types listed for AnnotationValue.
func (f *Field) InitialValue() (AnnotationValue, bool) {
	defer runtime.KeepAlive(f)
	var v C.DexppValue
	if C.dexpp_field_initial_value(f.p(), &v) == 0 {
		return nil, false
	}
	defer C.dexpp_value_clear(&v)
	return goValue(&v), true
}

// ---- Annotations and values ---------------------------------------------- //

// Annotation is one annotation on a class, a method or a field. It is a copy
// of the data, so it stays valid on its own.
type Annotation struct {
	// TypeDescriptor is the annotation's class, such as
	// "Ljava/lang/Deprecated;".
	TypeDescriptor string
	// Visibility says who can see the annotation.
	Visibility AnnotationVisibility
	// Elements are the elements the source sets, in DEX order (sorted by
	// name). An element left at its default value is not listed: the DEX
	// format stores defaults once, on the annotation's own class.
	Elements []AnnotationElement
}

// AnnotationElement is one "name = value" pair of an annotation.
type AnnotationElement struct {
	Name  string
	Value AnnotationValue
}

// Element returns the value of the element with this name. The bool is false
// when the annotation does not list it, which is also the case for an element
// left at its default value.
func (a Annotation) Element(name string) (AnnotationValue, bool) {
	for _, e := range a.Elements {
		if e.Name == name {
			return e.Value, true
		}
	}
	return nil, false
}

// AnnotationValue is the value of an annotation element, and the type of
// Field.InitialValue. It holds one of these types; use a type switch to tell
// them apart:
//
//   - nil, for a null value;
//   - bool;
//   - int64, for every integer type (byte, short, char, int, long);
//   - float64, for float and double;
//   - string;
//   - TypeRef, EnumRef, FieldRef, MethodRef, MethodTypeRef or
//     MethodHandleRef, for a value that refers to something;
//   - []AnnotationValue, for an array;
//   - Annotation, for a nested annotation.
//
// The original width of a number is not kept.
type AnnotationValue = any

// TypeRef is a class literal as a value, such as String.class.
type TypeRef struct {
	Descriptor string // the type, such as "Ljava/lang/String;"
}

// EnumRef is an enum constant as a value.
type EnumRef struct {
	Constant FieldID // the static field that holds the enum constant
}

// FieldRef is a reference to a field as a value. It is rare outside system
// annotations.
type FieldRef struct {
	Field FieldID
}

// MethodRef is a reference to a method as a value. It is rare outside system
// annotations.
type MethodRef struct {
	Method MethodID
}

// MethodTypeRef is a method prototype as a value.
type MethodTypeRef struct {
	Proto string // the prototype, such as "(I)V"
}

// MethodHandleRef is a method handle as a value.
type MethodHandleRef struct {
	// Index is the position of the handle in the DEX file's method handle
	// table. It is not resolved further.
	Index uint32
}

// goValue converts a C value to Go. It does not free the C value.
func goValue(v *C.DexppValue) AnnotationValue {
	switch v.kind {
	case C.DEXPP_VALUE_NULL:
		return nil
	case C.DEXPP_VALUE_BOOL:
		return v.bool_value != 0
	case C.DEXPP_VALUE_INT:
		return int64(v.int_value)
	case C.DEXPP_VALUE_FLOAT:
		return float64(v.float_value)
	case C.DEXPP_VALUE_STRING:
		return goText(v.string_value)
	case C.DEXPP_VALUE_TYPE:
		return TypeRef{Descriptor: C.GoString(v.type_descriptor)}
	case C.DEXPP_VALUE_ENUM:
		return EnumRef{Constant: goFieldID(v.field)}
	case C.DEXPP_VALUE_FIELD:
		return FieldRef{Field: goFieldID(v.field)}
	case C.DEXPP_VALUE_METHOD:
		return MethodRef{Method: goMethodID(v.method)}
	case C.DEXPP_VALUE_METHOD_TYPE:
		return MethodTypeRef{Proto: C.GoString(v.proto)}
	case C.DEXPP_VALUE_METHOD_HANDLE:
		return MethodHandleRef{Index: uint32(v.method_handle_index)}
	case C.DEXPP_VALUE_ARRAY:
		out := make([]AnnotationValue, int(v.element_count))
		if v.elements != nil {
			elements := unsafe.Slice(v.elements, int(v.element_count))
			for i := range elements {
				out[i] = goValue(&elements[i])
			}
		}
		return out
	case C.DEXPP_VALUE_ANNOTATION:
		if v.annotation == nil {
			return Annotation{}
		}
		return goAnnotation(v.annotation)
	}
	return nil // a kind this package does not know yet
}

// goAnnotation converts a C annotation to Go. It does not free it.
func goAnnotation(a *C.DexppAnnotation) Annotation {
	out := Annotation{
		TypeDescriptor: C.GoString(a.type_descriptor),
		Visibility:     AnnotationVisibility(a.visibility),
	}
	if a.element_count > 0 {
		elements := unsafe.Slice(a.elements, int(a.element_count))
		out.Elements = make([]AnnotationElement, len(elements))
		for i := range elements {
			out.Elements[i] = AnnotationElement{
				Name:  C.GoString(elements[i].name),
				Value: goValue(&elements[i].value),
			}
		}
	}
	return out
}

// takeAnnotations copies a C annotation array into Go and frees it.
func takeAnnotations(arr *C.DexppAnnotation, n C.size_t) []Annotation {
	defer C.dexpp_annotation_array_free(arr, n)
	if arr == nil || n == 0 {
		return nil
	}
	items := unsafe.Slice(arr, int(n))
	out := make([]Annotation, len(items))
	for i := range items {
		out[i] = goAnnotation(&items[i])
	}
	return out
}
