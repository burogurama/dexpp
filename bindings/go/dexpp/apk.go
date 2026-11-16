package dexpp

// The APK: archive entries, the manifest, resources and signing information.

/*
#include <stdlib.h>
#include "dexpp.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Apk is an Android package: the archive, its manifest, its resources and its
// signatures. The code is a separate object, which Analysis returns. The
// manifest, the resources and the signing information are each decoded on
// first use and kept. It is a handle (see the package documentation).
type Apk struct{ ptr *C.DexppApk }

// OpenAPK opens an APK file. It fails with CodeFileNotFound, CodeCannotRead
// or CodeInvalidAPK (the file is not a ZIP archive).
func OpenAPK(path string) (*Apk, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var cerr *C.DexppError
	p := C.dexpp_apk_open(cpath, &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newApk(p), nil
}

// OpenAPKBytes opens an APK from its bytes in memory, for example bytes
// downloaded from the network. The bytes get the same checks as a file given
// to OpenAPK, and the Apk then works the same. The Apk keeps its own copy, so
// data may be changed or reused as soon as OpenAPKBytes returns.
//
// label names the bytes at the start of error messages, where OpenAPK puts the
// path, for example the name of the file they came from; Analysis uses it too.
// An empty label gives "<apk>". It fails with CodeInvalidAPK (the bytes are not
// a ZIP archive).
func OpenAPKBytes(data []byte, label string) (*Apk, error) {
	var ptr *C.uint8_t
	if len(data) > 0 {
		// cgo lets C read Go memory that holds no Go pointers during the call.
		ptr = (*C.uint8_t)(unsafe.Pointer(&data[0]))
	}
	var clabel *C.char
	if label != "" {
		clabel = C.CString(label)
		defer C.free(unsafe.Pointer(clabel))
	}
	var cerr *C.DexppError
	p := C.dexpp_apk_open_buffer(ptr, C.size_t(len(data)), clabel, &cerr)
	runtime.KeepAlive(data)
	if p == nil {
		return nil, goError(cerr)
	}
	return newApk(p), nil
}

func newApk(p *C.DexppApk) *Apk {
	a := &Apk{ptr: p}
	runtime.SetFinalizer(a, (*Apk).Close)
	return a
}

// Close frees the APK handle at once. A Context that came from Analysis stays
// valid. Calling Close again, or on a nil *Apk, does nothing.
func (a *Apk) Close() {
	if a == nil || a.ptr == nil {
		return
	}
	C.dexpp_apk_free(a.ptr)
	a.ptr = nil
	runtime.SetFinalizer(a, nil)
}

func (a *Apk) p() *C.DexppApk {
	if a == nil {
		panicNil("Apk")
	}
	if a.ptr == nil {
		panicClosed("Apk")
	}
	return a.ptr
}

// Entries returns the name of every entry in the archive.
func (a *Apk) Entries() []string {
	defer runtime.KeepAlive(a)
	var n C.size_t
	arr := C.dexpp_apk_entries(a.p(), &n)
	return takeStringArray(arr, n)
}

// Read returns the uncompressed bytes of one archive entry. The bool is false
// when the archive has no entry with this name. An empty entry gives an empty
// slice and true.
func (a *Apk) Read(name string) ([]byte, bool) {
	defer runtime.KeepAlive(a)
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	var buf *C.uint8_t
	var n C.size_t
	if C.dexpp_apk_read(a.p(), cname, &buf, &n) == 0 {
		return nil, false
	}
	defer C.dexpp_buffer_free(buf)
	out := goBytes(unsafe.Pointer(buf), n)
	if out == nil {
		out = []byte{}
	}
	return out, true
}

// Package returns the app's package name, from the manifest. The bool is
// false when the APK has no manifest or the manifest cannot be decoded.
func (a *Apk) Package() (string, bool) {
	defer runtime.KeepAlive(a)
	return takeOptString(C.dexpp_apk_package(a.p()))
}

// Analysis returns the code of the APK: a Context over every classes*.dex
// entry, in multidex order. It reuses the archive bytes the Apk already holds.
// The options are those of FromAPK: WithThreads(1) keeps all work, the call
// graph included, on the calling thread.
//
// The analysis is built on the first call and kept, one for each thread count:
// a later call with the same count returns a new Context for the same
// analysis, so its call graph and other structures are built once. A call with
// another count builds a second analysis, which holds its own copy of the DEX
// files. So use one count in a program.
//
// It fails with CodeInvalidAPK (the APK has no classes.dex, or an entry is
// damaged) or CodeInvalidDex.
func (a *Apk) Analysis(opts ...LoadOption) (*Context, error) {
	defer runtime.KeepAlive(a)
	o := applyLoadOptions(opts)
	var cerr *C.DexppError
	p := C.dexpp_apk_analysis(a.p(), C.uint32_t(o.threads), &cerr)
	if p == nil {
		return nil, goError(cerr)
	}
	return newContext(p), nil
}

// ---- Manifest ------------------------------------------------------------ //

// IntentFilter is an <intent-filter> of a component.
type IntentFilter struct {
	Actions    []string // the android:name of each <action>
	Categories []string // the android:name of each <category>
	// IsLauncher is true for the filter that puts an activity in the
	// launcher: action android.intent.action.MAIN with category
	// android.intent.category.LAUNCHER.
	IsLauncher bool
}

// HasAction reports whether the filter lists this action.
func (f IntentFilter) HasAction(name string) bool {
	for _, a := range f.Actions {
		if a == name {
			return true
		}
	}
	return false
}

// Component is an activity, a service, a broadcast receiver or a content
// provider declared in the manifest.
type Component struct {
	// Name is the component's class name with its package, such as
	// "com.example.MainActivity". A short name in the manifest, such as
	// ".MainActivity", is completed with the app's package. It is empty when
	// the attribute is missing, or when it is a reference that gives no
	// single value (then NameReference is set).
	Name string
	// NameReference is the resource id when android:name is written as a
	// reference, such as "@string/main_activity", whether it was resolved or
	// not. It is nil for a plain value or a missing attribute.
	NameReference *uint32
	Kind          ComponentKind
	// Exported is the android:exported attribute: the value the manifest
	// writes, or the value of the resource it refers to in the default
	// configuration. Text reads as Android reads it: only "1", "true" and
	// "TRUE" are true. It is nil when the attribute is missing, or when it
	// is a reference that could not be resolved (then ExportedReference is
	// set). IsExported gives the effective answer.
	Exported *bool
	// ExportedReference is the resource id when android:exported is written
	// as a reference, such as "@bool/is_exported" (or a theme attribute,
	// "?attr/name"), whether it was resolved or not. It is nil for a plain
	// value or a missing attribute. Exported nil with ExportedReference set
	// means the value is unknown.
	ExportedReference *uint32
	// ExportedVaries is true when android:exported is a reference whose
	// value is not the same in every resource configuration: for example
	// false in res/values and true in res/values-v31, so the component is
	// exported on Android 12 and newer. Exported holds the default
	// configuration's value, and IsExported is true.
	ExportedVaries bool
	// Permission is the permission a caller must hold to use the component,
	// or nil when there is none. It is also nil when android:permission is a
	// reference that gives no single value (then PermissionReference is
	// set): Android ignores a string that changes with the language, the
	// screen or another configuration, and a string that changes only with
	// the Android version depends on the device.
	Permission *string
	// PermissionReference is the resource id when android:permission is
	// written as a reference, such as "@string/guard", whether it was
	// resolved or not. It is nil for a plain value or a missing attribute.
	PermissionReference *uint32
	IntentFilters       []IntentFilter
	// GuardedBySignaturePermission is true when Permission is a permission
	// this app declares with the signature or signatureOrSystem protection
	// level. Only apps signed with the same key can hold such a permission.
	GuardedBySignaturePermission bool
	// IsExported says whether other apps can address the component: the
	// Exported attribute when the manifest sets it, otherwise true when the
	// component has an intent filter. When the attribute is a reference whose
	// value differs between configurations (ExportedVaries), IsExported is
	// true. When the attribute is a reference that could not be resolved, its
	// value is unknown and IsExported is true: the component may be
	// exported.
	IsExported bool
	// IsReachable says whether an app from another developer can reach the
	// component: it is exported, and it is not guarded by a signature-level
	// permission that this app declares. A component guarded by a normal or
	// dangerous permission stays reachable, because any app can request
	// those. So does a component guarded by a permission the manifest does
	// not declare, because its protection level is not known.
	IsReachable bool
}

// DeclaredPermission is a <permission> that the app itself declares.
type DeclaredPermission struct {
	Name string
	// ProtectionLevel is the android:protectionLevel as the raw Android
	// value, or nil when it is not known: the attribute is a resource
	// reference that could not be resolved. The low four bits are the base
	// level: 0 normal, 1 dangerous, 2 signature, 3 signatureOrSystem. Higher
	// bits are flags. A declaration without the attribute counts as normal.
	// For a reference, it is the default configuration's value.
	ProtectionLevel *int32
	// ProtectionLevelVaries is true when the level is a reference whose
	// value is not the same in every configuration. The permission then
	// guards a component only when every configuration gives signature or
	// signatureOrSystem.
	ProtectionLevelVaries bool
}

// Manifest is the decoded AndroidManifest.xml. A field the manifest may leave
// out is nil when it is not set.
//
// An attribute can be written as a resource reference, such as
// android:debuggable="@bool/debug". Its field then holds the value the APK's
// resource table gives, in the default configuration, and the matching
// ...Reference field holds the reference id. When the reference cannot be
// resolved, the value field is nil and the ...Reference field is still set:
// the attribute is written, but its value is unknown. A ...Reference field is
// nil for a plain value or a missing attribute.
//
// A boolean whose resource has another value in some configuration (a
// language, a screen size, an Android version) has a matching ...Varies field
// that is true. Integers hold the default configuration's value. Text, such as
// VersionName, is nil when its string changes with the configuration.
type Manifest struct {
	Package              string  // the package attribute of <manifest>
	VersionCode          *int64  // android:versionCode
	VersionCodeReference *uint32 // the reference id of android:versionCode
	VersionName          *string // android:versionName
	VersionNameReference *uint32 // the reference id of android:versionName
	MinSDK               *int64  // android:minSdkVersion of <uses-sdk>
	MinSDKReference      *uint32 // the reference id of android:minSdkVersion
	TargetSDK            *int64  // android:targetSdkVersion of <uses-sdk>
	TargetSDKReference   *uint32 // the reference id of android:targetSdkVersion
	// Permissions are the permissions the app asks for: the name of each
	// <uses-permission>.
	Permissions []string
	// DeclaredPermissions are the permissions the app itself declares, in
	// manifest order.
	DeclaredPermissions []DeclaredPermission
	// Components are every activity, service, receiver and provider, in
	// manifest order.
	Components []Component
	// LauncherActivity is the name of the activity the launcher starts.
	LauncherActivity    *string
	Debuggable          *bool   // android:debuggable of <application>
	DebuggableReference *uint32 // the reference id of android:debuggable
	// DebuggableVaries is true when android:debuggable is a reference whose
	// value is not the same in every configuration. Debuggable holds the
	// default configuration's value; for triage, treat a varying value as
	// possibly true.
	DebuggableVaries     bool
	AllowBackup          *bool   // android:allowBackup of <application>
	AllowBackupReference *uint32 // the reference id of android:allowBackup
	// AllowBackupVaries is true when android:allowBackup is a reference whose
	// value is not the same in every configuration, like DebuggableVaries.
	AllowBackupVaries bool
}

// ComponentsOf returns the components of one kind, in manifest order.
func (m Manifest) ComponentsOf(kind ComponentKind) []Component {
	var out []Component
	for _, c := range m.Components {
		if c.Kind == kind {
			out = append(out, c)
		}
	}
	return out
}

// DeclaredPermissionProtectionLevel returns the android:protectionLevel of a
// permission the app declares, as the raw Android value (see
// DeclaredPermission). The bool is false when the app does not declare this
// permission, for example for a platform permission.
func (m Manifest) DeclaredPermissionProtectionLevel(name string) (int32, bool) {
	for _, p := range m.DeclaredPermissions {
		if p.Name == name && p.ProtectionLevel != nil {
			return *p.ProtectionLevel, true
		}
	}
	return 0, false
}

// DeclaredPermissionProtectionLevelVaries reports whether the app declares
// this permission with a protection level given by a reference whose value is
// not the same in every configuration (see DeclaredPermission). It is false
// for a permission the app does not declare.
func (m Manifest) DeclaredPermissionProtectionLevelVaries(name string) bool {
	for _, p := range m.DeclaredPermissions {
		if p.Name == name {
			return p.ProtectionLevelVaries
		}
	}
	return false
}

func optInt64(has C.int, v C.int64_t) *int64 {
	if has == 0 {
		return nil
	}
	out := int64(v)
	return &out
}

// optReference converts a C reference id (0 for none).
func optReference(id C.uint32_t) *uint32 {
	if id == 0 {
		return nil
	}
	out := uint32(id)
	return &out
}

// optBool converts a C tri-state (1, 0, or -1 for absent).
func optBool(v C.int) *bool {
	if v < 0 {
		return nil
	}
	out := v != 0
	return &out
}

func goComponent(c *C.DexppComponent) Component {
	out := Component{
		Name:                         C.GoString(c.name),
		NameReference:                optReference(c.name_reference),
		Kind:                         ComponentKind(c.kind),
		Exported:                     optBool(c.exported),
		ExportedReference:            optReference(c.exported_reference),
		ExportedVaries:               c.exported_varies != 0,
		Permission:                   goOptString(c.permission),
		PermissionReference:          optReference(c.permission_reference),
		GuardedBySignaturePermission: c.guarded_by_signature_permission != 0,
		IsExported:                   c.is_exported != 0,
		IsReachable:                  c.is_reachable != 0,
	}
	if c.intent_filters != nil && c.intent_filter_count > 0 {
		filters := unsafe.Slice(c.intent_filters, int(c.intent_filter_count))
		out.IntentFilters = make([]IntentFilter, len(filters))
		for i := range filters {
			f := &filters[i]
			out.IntentFilters[i] = IntentFilter{
				Actions:    goStrings(f.actions, f.action_count),
				Categories: goStrings(f.categories, f.category_count),
				IsLauncher: f.is_launcher != 0,
			}
		}
	}
	return out
}

// Manifest returns the decoded manifest. The bool is false when the APK has
// no manifest or it cannot be decoded.
func (a *Apk) Manifest() (Manifest, bool) {
	defer runtime.KeepAlive(a)
	var m C.DexppManifest
	if C.dexpp_apk_manifest(a.p(), &m) == 0 {
		return Manifest{}, false
	}
	defer C.dexpp_manifest_clear(&m)
	out := Manifest{
		Package:              C.GoString(m._package),
		VersionCode:          optInt64(m.has_version_code, m.version_code),
		VersionCodeReference: optReference(m.version_code_reference),
		VersionName:          goOptString(m.version_name),
		VersionNameReference: optReference(m.version_name_reference),
		MinSDK:               optInt64(m.has_min_sdk, m.min_sdk),
		MinSDKReference:      optReference(m.min_sdk_reference),
		TargetSDK:            optInt64(m.has_target_sdk, m.target_sdk),
		TargetSDKReference:   optReference(m.target_sdk_reference),
		Permissions:          goStrings(m.permissions, m.permission_count),
		LauncherActivity:     goOptString(m.launcher_activity),
		Debuggable:           optBool(m.debuggable),
		DebuggableReference:  optReference(m.debuggable_reference),
		DebuggableVaries:     m.debuggable_varies != 0,
		AllowBackup:          optBool(m.allow_backup),
		AllowBackupReference: optReference(m.allow_backup_reference),
		AllowBackupVaries:    m.allow_backup_varies != 0,
	}
	if m.declared_permissions != nil && m.declared_permission_count > 0 {
		declared := unsafe.Slice(m.declared_permissions, int(m.declared_permission_count))
		out.DeclaredPermissions = make([]DeclaredPermission, len(declared))
		for i, p := range declared {
			out.DeclaredPermissions[i].Name = C.GoString(p.name)
			out.DeclaredPermissions[i].ProtectionLevelVaries = p.protection_level_varies != 0
			if p.has_protection_level != 0 {
				level := int32(p.protection_level)
				out.DeclaredPermissions[i].ProtectionLevel = &level
			}
		}
	}
	if m.components != nil && m.component_count > 0 {
		components := unsafe.Slice(m.components, int(m.component_count))
		out.Components = make([]Component, len(components))
		for i := range components {
			out.Components[i] = goComponent(&components[i])
		}
	}
	return out, true
}

// ---- Resources ----------------------------------------------------------- //

// ResourceName is the full name of a resource: package:type/entry.
type ResourceName struct {
	Package string // such as "com.example.app"
	Type    string // such as "string" or "drawable"
	Entry   string // such as "app_name"
}

// String writes the name as "package:type/entry".
func (n ResourceName) String() string { return n.Package + ":" + n.Type + "/" + n.Entry }

// ResourceValue is the value of a resource in one configuration, as the table
// stores it. A value that refers to another resource is not followed.
type ResourceValue struct {
	// Configuration is written like the qualifiers of a res/ folder name: ""
	// for the default configuration (res/values), "v31" for Android 12 and
	// newer, "fr" for French, "b+sr+Latn-land-night-v8" for a combination.
	// The text is the one `aapt2 dump resources` prints.
	Configuration string
	// Type is the Res_value data type from Android's ResourceTypes.h: for
	// example 0x01 a reference to another resource, 0x03 a string, 0x10 a
	// decimal integer, 0x11 a hexadecimal integer, 0x12 a boolean.
	Type uint8
	// Data is the raw 32-bit data: the id of the resource a reference names,
	// the integer itself, 0 or not 0 for a boolean (aapt2 writes true as
	// 0xFFFFFFFF), or the index of a string in the table's string pool.
	Data uint32
	// String is the text of a string value (type 0x03), or nil for any other
	// type.
	String *string

	boolean *bool
	integer *int64
}

// AsBool returns the value read as a boolean, the way Android reads a
// boolean attribute. An integer-typed value is true when it is not 0. A
// string is true when it is exactly "1", "true" or "TRUE", and false
// otherwise. A floating-point value, a dimension or a fraction is false. The
// second result is false for a reference (not followed here), a null value or
// an empty string, for which Android uses the attribute's default.
func (v ResourceValue) AsBool() (bool, bool) {
	if v.boolean == nil {
		return false, false
	}
	return *v.boolean, true
}

// AsInt returns the value read as an integer: any integer-typed value (an
// integer, a boolean, a color), as its signed 32-bit data. The second result
// is false for any other type.
func (v ResourceValue) AsInt() (int64, bool) {
	if v.integer == nil {
		return 0, false
	}
	return *v.integer, true
}

// ResourceTable is the decoded resources.arsc table of an APK. It maps
// resource ids to names and to string, boolean and integer values. A resource
// can have a value in several configurations, for example one per language:
// Values lists them all, and the Resolve methods use the value of the default
// configuration. They follow a resource whose value refers to another
// resource to the end, for up to 20 steps as Android does. It reads through
// the Apk it came from, so it needs that Apk to stay open.
type ResourceTable struct{ apk *Apk }

// Resources returns the decoded resource table, or nil when the APK has none
// or it cannot be decoded.
func (a *Apk) Resources() *ResourceTable {
	defer runtime.KeepAlive(a)
	if C.dexpp_apk_has_resources(a.p()) == 0 {
		return nil
	}
	return &ResourceTable{apk: a}
}

func (t *ResourceTable) p() *C.DexppApk {
	if t == nil {
		panicNil("ResourceTable")
	}
	return t.apk.p()
}

// Empty reports whether the table has no entries.
func (t *ResourceTable) Empty() bool {
	defer runtime.KeepAlive(t)
	return C.dexpp_apk_resources_empty(t.p()) != 0
}

// NameOf returns the name of the resource with this id. The bool is false
// when the table has no such id.
func (t *ResourceTable) NameOf(id uint32) (ResourceName, bool) {
	defer runtime.KeepAlive(t)
	var out C.DexppResourceName
	if C.dexpp_apk_resource_name(t.p(), C.uint32_t(id), &out) == 0 {
		return ResourceName{}, false
	}
	defer C.dexpp_resource_name_clear(&out)
	return ResourceName{C.GoString(out._package), C.GoString(out._type), C.GoString(out.entry)}, true
}

// ResolveString returns the value of a string resource. The bool is false
// when the id is missing or its value is not a string.
func (t *ResourceTable) ResolveString(id uint32) (string, bool) {
	if t == nil {
		panicNil("ResourceTable")
	}
	return t.apk.ResolveString(id)
}

// Values returns every value of the resource with this id, one for each
// configuration the table gives it, in table order. It is empty when the
// table has no such id. A complex entry, such as a style, an array or a
// plural, has no single value and is left out. A value that refers to
// another resource is returned as it is: call Values with its Data to follow
// it.
func (t *ResourceTable) Values(id uint32) []ResourceValue {
	defer runtime.KeepAlive(t)
	var count C.size_t
	values := C.dexpp_apk_resource_values(t.p(), C.uint32_t(id), &count)
	if values == nil || count == 0 {
		return nil
	}
	defer C.dexpp_resource_value_array_free(values, count)
	out := make([]ResourceValue, int(count))
	for i, v := range unsafe.Slice(values, int(count)) {
		out[i] = ResourceValue{
			Configuration: C.GoString(v.configuration),
			Type:          uint8(v._type),
			Data:          uint32(v.data),
			boolean:       optBool(v.boolean),
			integer:       optInt64(v.has_integer, v.integer),
		}
		if v.string.data != nil {
			s := C.GoStringN(v.string.data, C.int(v.string.length))
			out[i].String = &s
		}
	}
	return out
}

// ResolveBool returns the value of a resource as a boolean, read as Android
// reads it (see ResourceValue.AsBool): a bool resource, any integer-typed
// value (nonzero is true), or a string ("1", "true" and "TRUE" are true, any
// other text is false). The second result is false when the id is missing, or
// its value is null or an empty string.
func (t *ResourceTable) ResolveBool(id uint32) (bool, bool) {
	defer runtime.KeepAlive(t)
	var out C.int
	if C.dexpp_apk_resolve_bool(t.p(), C.uint32_t(id), &out) == 0 {
		return false, false
	}
	return out != 0, true
}

// ResolveInt returns the value of a resource as an integer: an integer
// resource, or any other integer-typed value, read as its signed 32-bit data.
// The bool is false when the id is missing or its value is of another type.
func (t *ResourceTable) ResolveInt(id uint32) (int64, bool) {
	defer runtime.KeepAlive(t)
	var out C.int64_t
	if C.dexpp_apk_resolve_int(t.p(), C.uint32_t(id), &out) == 0 {
		return 0, false
	}
	return int64(out), true
}

// IDOf returns the id of the resource with this type and entry name, such as
// ("string", "app_name"). The bool is false when there is none. It scans the
// table, so it is meant for single lookups, not for loops.
func (t *ResourceTable) IDOf(typ, entry string) (uint32, bool) {
	defer runtime.KeepAlive(t)
	ct, ce := C.CString(typ), C.CString(entry)
	defer C.free(unsafe.Pointer(ct))
	defer C.free(unsafe.Pointer(ce))
	var id C.uint32_t
	if C.dexpp_apk_resource_id(t.p(), ct, ce, &id) == 0 {
		return 0, false
	}
	return uint32(id), true
}

// ResolveString returns the value of a string resource, through the resource
// table. Use it for a manifest attribute that refers to a resource, such as
// android:label="@string/app_name". The bool is false when the APK has no
// resource table, or the id is not a string resource.
func (a *Apk) ResolveString(id uint32) (string, bool) {
	defer runtime.KeepAlive(a)
	var out C.DexppText
	if C.dexpp_apk_resolve_string(a.p(), C.uint32_t(id), &out) == 0 {
		return "", false
	}
	defer C.dexpp_text_clear(&out)
	return goText(out), true
}

// ---- Signing ------------------------------------------------------------- //

// Certificate is an X.509 certificate that signed an APK.
type Certificate struct {
	DER        []byte // the certificate in its binary (DER) form
	Subject    string // who the certificate is for, such as "C=US, O=Example, CN=App"
	Issuer     string // who issued it, in the same form; equal to Subject when self-signed
	SerialHex  string // the serial number, in lowercase hex
	NotBefore  string // start of the validity period, as "YYYY-MM-DDTHH:MM:SSZ"
	NotAfter   string // end of the validity period, in the same form
	SHA1Hex    string // SHA-1 fingerprint of the DER bytes, in lowercase hex
	SHA256Hex  string // SHA-256 fingerprint of the DER bytes, in lowercase hex
	SelfSigned bool   // true when the subject and the issuer are the same
}

// SigningInfo says which signature schemes an APK carries, and holds the
// certificates in them. This is identification only: the signatures and
// digests are not verified.
type SigningInfo struct {
	V1 bool // a JAR signature: a META-INF/*.RSA, *.DSA or *.EC file
	V2 bool // an APK Signature Scheme v2 block
	V3 bool // an APK Signature Scheme v3 or v3.1 block
	// Certificates are the signing certificates of all schemes. A certificate
	// that appears in several schemes is listed once. Empty for an unsigned
	// APK.
	Certificates []Certificate
}

// IsSigned reports whether at least one signature scheme is present.
func (s SigningInfo) IsSigned() bool { return s.V1 || s.V2 || s.V3 }

// Signing returns the signature schemes and certificates of the APK. An
// unsigned APK gives a SigningInfo whose IsSigned is false.
func (a *Apk) Signing() SigningInfo {
	defer runtime.KeepAlive(a)
	var info C.DexppSigningInfo
	C.dexpp_apk_signing(a.p(), &info)
	defer C.dexpp_signing_info_clear(&info)
	out := SigningInfo{V1: info.v1 != 0, V2: info.v2 != 0, V3: info.v3 != 0}
	if info.certificates != nil && info.certificate_count > 0 {
		certs := unsafe.Slice(info.certificates, int(info.certificate_count))
		out.Certificates = make([]Certificate, len(certs))
		for i := range certs {
			c := &certs[i]
			out.Certificates[i] = Certificate{
				DER:        goBytes(unsafe.Pointer(c.der), c.der_length),
				Subject:    C.GoString(c.subject),
				Issuer:     C.GoString(c.issuer),
				SerialHex:  C.GoString(c.serial_hex),
				NotBefore:  C.GoString(c.not_before),
				NotAfter:   C.GoString(c.not_after),
				SHA1Hex:    C.GoString(c.sha1_hex),
				SHA256Hex:  C.GoString(c.sha256_hex),
				SelfSigned: c.self_signed != 0,
			}
		}
	}
	return out
}
