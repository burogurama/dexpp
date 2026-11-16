package dexpp

import (
	"archive/zip"
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"
)

func mustAPK(t *testing.T, name string) *Apk {
	t.Helper()
	apk, err := OpenAPK(data(name))
	if err != nil {
		t.Fatalf("OpenAPK(%s): %v", name, err)
	}
	t.Cleanup(apk.Close)
	return apk
}

func mustManifest(t *testing.T, apk *Apk) Manifest {
	t.Helper()
	m, ok := apk.Manifest()
	if !ok {
		t.Fatal("no manifest")
	}
	return m
}

func TestArchiveEntries(t *testing.T) {
	apk := mustAPK(t, "sample.apk")
	if !slices.Contains(apk.Entries(), "classes.dex") {
		t.Fatalf("entries = %v", apk.Entries())
	}
	dex, ok := apk.Read("classes.dex")
	if !ok || !bytes.HasPrefix(dex, []byte("dex\n")) {
		t.Errorf("Read(classes.dex) = %d bytes, %v", len(dex), ok)
	}
	if got, ok := apk.Read("nope"); ok || got != nil {
		t.Errorf("Read(nope) = %d bytes, %v", len(got), ok)
	}
}

func TestManifest(t *testing.T) {
	apk := mustAPK(t, "sample.apk")
	m := mustManifest(t, apk)
	if pkg, ok := apk.Package(); !ok || pkg != "com.example.dexpp" || m.Package != pkg {
		t.Errorf("Package = %q, %v; manifest says %q", pkg, ok, m.Package)
	}
	if m.VersionCode == nil || *m.VersionCode != 7 || m.VersionName == nil || *m.VersionName != "1.2.3" {
		t.Errorf("version = %v, %v", m.VersionCode, m.VersionName)
	}
	if m.MinSDK == nil || *m.MinSDK != 21 || m.TargetSDK == nil || *m.TargetSDK != 34 {
		t.Errorf("SDK levels = %v, %v", m.MinSDK, m.TargetSDK)
	}
	if !slices.Contains(m.Permissions, "android.permission.INTERNET") {
		t.Errorf("Permissions = %v", m.Permissions)
	}
	if len(m.DeclaredPermissions) != 1 || m.DeclaredPermissions[0].Name != "com.example.dexpp.CUSTOM" {
		t.Errorf("DeclaredPermissions = %+v", m.DeclaredPermissions)
	}
	if m.Debuggable == nil || !*m.Debuggable || m.AllowBackup == nil || *m.AllowBackup {
		t.Errorf("debuggable = %v, allowBackup = %v", m.Debuggable, m.AllowBackup)
	}
	if m.LauncherActivity == nil || *m.LauncherActivity != "com.example.dexpp.MainActivity" {
		t.Errorf("LauncherActivity = %v", m.LauncherActivity)
	}

	activities := m.ComponentsOf(ComponentActivity)
	if len(activities) != 1 || !activities[0].IsExported || activities[0].Kind != ComponentActivity {
		t.Fatalf("activities = %+v", activities)
	}
	filters := activities[0].IntentFilters
	if len(filters) == 0 || !filters[0].IsLauncher || !filters[0].HasAction("android.intent.action.MAIN") ||
		filters[0].HasAction("android.intent.action.VIEW") {
		t.Errorf("intent filters = %+v", filters)
	}
	if !slices.Contains(filters[0].Categories, "android.intent.category.LAUNCHER") {
		t.Errorf("categories = %v", filters[0].Categories)
	}
	var service *Component
	for i := range m.Components {
		if strings.HasSuffix(m.Components[i].Name, "SyncService") {
			service = &m.Components[i]
		}
	}
	if service == nil || service.IsExported || service.Kind != ComponentService {
		t.Errorf("SyncService = %+v", service)
	}
	total := 0
	for _, kind := range []ComponentKind{ComponentActivity, ComponentService, ComponentReceiver,
		ComponentProvider} {
		total += len(m.ComponentsOf(kind))
	}
	if total != len(m.Components) {
		t.Errorf("ComponentsOf covers %d of %d components", total, len(m.Components))
	}
}

func TestReachability(t *testing.T) {
	m := mustManifest(t, mustAPK(t, "reachable.apk"))
	for name, want := range map[string]int32{
		"com.dexpp.reach.P_NORMAL": 0, "com.dexpp.reach.P_DANGEROUS": 1, "com.dexpp.reach.P_SIGNATURE": 2,
	} {
		if got, ok := m.DeclaredPermissionProtectionLevel(name); !ok || got != want {
			t.Errorf("protection level of %s = %d, %v; want %d", name, got, ok, want)
		}
	}
	// An undeclared permission, such as a platform one, has no known level.
	if got, ok := m.DeclaredPermissionProtectionLevel("android.permission.DUMP"); ok {
		t.Errorf("android.permission.DUMP has level %d", got)
	}

	byName := map[string]Component{}
	for _, c := range m.Components {
		byName[c.Name[strings.LastIndex(c.Name, ".")+1:]] = c
	}
	// Guarded by its own normal, dangerous or undeclared permission:
	// reachable, although the application requires a signature permission.
	for _, name := range []string{"NormalActivity", "DangerousActivity",
		"PlatformPermActivity", "NormalReceiver"} {
		if !byName[name].IsReachable || byName[name].PermissionInherited {
			t.Errorf("%s = %+v", name, byName[name])
		}
	}
	// Exported, but guarded by a signature permission the app declares.
	sig := byName["SigActivity"]
	if !sig.IsExported || !sig.GuardedBySignaturePermission || sig.IsReachable || sig.Permission == nil {
		t.Errorf("SigActivity = %+v", sig)
	}
	if s := byName["SigService"]; !s.IsExported || s.IsReachable {
		t.Errorf("SigService = %+v", s)
	}
	// Not exported: not reachable.
	hidden := byName["HiddenActivity"]
	if hidden.IsExported || hidden.IsReachable {
		t.Errorf("HiddenActivity = %+v", hidden)
	}
	if hidden.Exported == nil || *hidden.Exported {
		t.Errorf("HiddenActivity android:exported = %v, want an explicit false", hidden.Exported)
	}

	exported, reachable := 0, 0
	for _, c := range m.ComponentsOf(ComponentActivity) {
		if c.IsExported {
			exported++
		}
		if c.IsReachable {
			reachable++
		}
	}
	if exported != 5 || reachable != 3 {
		t.Errorf("%d exported and %d reachable activities, want 5 and 3", exported, reachable)
	}
}

// reachable.apk's <application> requires its signature permission, and the
// components without a permission of their own take it.
func TestApplicationPermission(t *testing.T) {
	m := mustManifest(t, mustAPK(t, "reachable.apk"))
	const signature = "com.dexpp.reach.P_SIGNATURE"
	if m.ApplicationPermission == nil || *m.ApplicationPermission != signature ||
		m.ApplicationPermissionReference != nil || m.ApplicationPermissionUnknown {
		t.Errorf("ApplicationPermission = %v, reference %v, unknown %v", m.ApplicationPermission,
			m.ApplicationPermissionReference, m.ApplicationPermissionUnknown)
	}
	byName := map[string]Component{}
	for _, c := range m.Components {
		byName[c.Name[strings.LastIndex(c.Name, ".")+1:]] = c
	}
	for _, name := range []string{"InheritingActivity", "InheritingService", "InheritingReceiver"} {
		c := byName[name]
		if c.Permission == nil || *c.Permission != signature || !c.PermissionInherited ||
			c.PermissionUnknown || c.PermissionReference != nil || !c.IsExported || c.IsReachable ||
			c.ReadPermission != nil || c.WritePermission != nil {
			t.Errorf("%s = %+v", name, c)
		}
	}
	// A provider is guarded only when reading and writing both are.
	inheriting := byName["InheritingProvider"]
	if inheriting.ReadPermission == nil || *inheriting.ReadPermission != signature ||
		inheriting.WritePermission == nil || *inheriting.WritePermission != signature ||
		inheriting.ReadPermissionUnknown || inheriting.WritePermissionUnknown || inheriting.IsReachable {
		t.Errorf("InheritingProvider = %+v", inheriting)
	}
	readable := byName["ReadableProvider"]
	if readable.ReadPermission == nil || *readable.ReadPermission != "com.dexpp.reach.P_NORMAL" ||
		readable.ReadPermissionReference != nil || !readable.IsReachable {
		t.Errorf("ReadableProvider = %+v", readable)
	}
	writable := byName["WritableProvider"]
	if writable.WritePermission == nil || *writable.WritePermission != "com.dexpp.reach.P_DANGEROUS" ||
		writable.WritePermissionReference != nil || writable.PermissionInherited || !writable.IsReachable {
		t.Errorf("WritableProvider = %+v", writable)
	}

	sample := mustManifest(t, mustAPK(t, "sample.apk"))
	if sample.ApplicationPermission != nil {
		t.Errorf("sample.apk ApplicationPermission = %v", *sample.ApplicationPermission)
	}
}

// refs.apk: @bool/nothing is @null, @bool/same_as_nothing refers to it, and
// @string/no_permission is @null. Android reads them as not written.
func TestManifestNullValues(t *testing.T) {
	apk := mustAPK(t, "refs.apk")
	rt := apk.Resources()
	if rt == nil {
		t.Fatal("no resource table")
	}
	m := mustManifest(t, apk)
	byName := map[string]Component{}
	for _, c := range m.Components {
		byName[c.Name[strings.LastIndex(c.Name, ".")+1:]] = c
	}
	nothing, _ := rt.IDOf("bool", "nothing")
	null := byName["NullActivity"]
	if null.Exported != nil || null.ExportedReference == nil || *null.ExportedReference != nothing ||
		null.ExportedUnknown || null.IsExported {
		t.Errorf("NullActivity = %+v", null)
	}
	if c := byName["NullFilteredActivity"]; c.Exported != nil || c.ExportedUnknown || !c.IsExported {
		t.Errorf("NullFilteredActivity = %+v", c)
	}
	if c := byName["LiteralNullActivity"]; c.Exported != nil || c.ExportedReference != nil || !c.IsExported {
		t.Errorf("LiteralNullActivity = %+v", c)
	}
	noPermission, _ := rt.IDOf("string", "no_permission")
	service := byName["NullPermissionService"]
	if service.Permission != nil || service.PermissionReference == nil ||
		*service.PermissionReference != noPermission || service.PermissionUnknown ||
		service.PermissionInherited || !service.IsReachable {
		t.Errorf("NullPermissionService = %+v", service)
	}
	// A theme attribute is unknown, so possibly exported.
	if theme := byName["ThemeActivity"]; theme.Exported != nil || !theme.ExportedUnknown || !theme.IsExported {
		t.Errorf("ThemeActivity = %+v", theme)
	}
	// A string that differs between Android versions depends on the device.
	if !byName["PermissionOnV31Service"].PermissionUnknown || byName["LocalizedPermissionService"].PermissionUnknown {
		t.Errorf("PermissionOnV31Service = %+v, LocalizedPermissionService = %+v",
			byName["PermissionOnV31Service"], byName["LocalizedPermissionService"])
	}
	if m.VersionCodeUnknown || m.VersionNameUnknown || m.MinSDKUnknown || m.TargetSDKUnknown ||
		m.DebuggableUnknown || m.AllowBackupUnknown || byName["ExportedActivity"].NameUnknown {
		t.Errorf("refs.apk has unknown values: %+v", m)
	}
}

func TestResources(t *testing.T) {
	apk := mustAPK(t, "sample.apk")
	rt := apk.Resources()
	if rt == nil || rt.Empty() {
		t.Fatal("no resource table")
	}
	appName, ok := rt.IDOf("string", "app_name")
	if !ok || appName>>24 != 0x7f {
		t.Fatalf("IDOf(string, app_name) = %#x, %v", appName, ok)
	}
	if got, ok := rt.ResolveString(appName); !ok || got != "Dexpp Sample" {
		t.Errorf("ResolveString = %q, %v", got, ok)
	}
	want := ResourceName{"com.example.dexpp", "string", "app_name"}
	if got, ok := rt.NameOf(appName); !ok || got != want || got.String() != "com.example.dexpp:string/app_name" {
		t.Errorf("NameOf = %v, %v", got, ok)
	}
	if _, ok := rt.IDOf("string", "nope"); ok {
		t.Error("IDOf(string, nope) found an id")
	}
	if got, ok := rt.ResolveString(0x7f999999); ok {
		t.Errorf("ResolveString(0x7f999999) = %q", got)
	}
	if _, ok := rt.NameOf(0x7f999999); ok {
		t.Error("NameOf(0x7f999999) found a name")
	}
	// The convenience on the Apk resolves through the same table.
	if got, ok := apk.ResolveString(appName); !ok || got != "Dexpp Sample" {
		t.Errorf("Apk.ResolveString = %q, %v", got, ok)
	}
	if _, ok := apk.ResolveString(0x7f999999); ok {
		t.Error("Apk.ResolveString(0x7f999999) found a string")
	}
}

// refs.apk writes its boolean and integer manifest attributes as resource
// references (tests/data/RefsManifest.xml).
func TestManifestReferences(t *testing.T) {
	apk := mustAPK(t, "refs.apk")
	rt := apk.Resources()
	if rt == nil {
		t.Fatal("no resource table")
	}
	id := func(typ, entry string) uint32 {
		t.Helper()
		v, ok := rt.IDOf(typ, entry)
		if !ok {
			t.Fatalf("IDOf(%s, %s) found nothing", typ, entry)
		}
		return v
	}
	m := mustManifest(t, apk)
	if m.MinSDK == nil || *m.MinSDK != 23 || m.MinSDKReference == nil || *m.MinSDKReference != id("integer", "min_sdk") {
		t.Errorf("MinSDK = %v, reference %v", m.MinSDK, m.MinSDKReference)
	}
	if m.TargetSDK == nil || *m.TargetSDK != 34 || m.TargetSDKReference == nil {
		t.Errorf("TargetSDK = %v, reference %v", m.TargetSDK, m.TargetSDKReference)
	}
	if m.Debuggable == nil || !*m.Debuggable || m.DebuggableReference == nil || *m.DebuggableReference != id("bool", "debug") {
		t.Errorf("Debuggable = %v, reference %v", m.Debuggable, m.DebuggableReference)
	}
	if m.AllowBackup == nil || *m.AllowBackup || m.AllowBackupReference == nil {
		t.Errorf("AllowBackup = %v, reference %v", m.AllowBackup, m.AllowBackupReference)
	}
	// A plain value has no reference.
	if m.VersionCode == nil || *m.VersionCode != 42 || m.VersionCodeReference != nil {
		t.Errorf("VersionCode = %v, reference %v", m.VersionCode, m.VersionCodeReference)
	}

	byName := map[string]Component{}
	for _, c := range m.Components {
		byName[c.Name[strings.LastIndex(c.Name, ".")+1:]] = c
	}
	// Resolved: true without an intent filter, false with one.
	exported := byName["ExportedActivity"]
	if exported.Exported == nil || !*exported.Exported || !exported.IsExported ||
		exported.ExportedReference == nil || *exported.ExportedReference != id("bool", "yes") {
		t.Errorf("ExportedActivity = %+v", exported)
	}
	if hidden := byName["HiddenActivity"]; hidden.Exported == nil || *hidden.Exported || hidden.IsExported {
		t.Errorf("HiddenActivity = %+v", hidden)
	}
	// A reference to the string "true": Android reads it as true.
	text := byName["TextActivity"]
	if text.Exported == nil || !*text.Exported || text.ExportedReference == nil || *text.ExportedReference != id("string", "not_a_bool") ||
		!text.IsExported || !text.IsReachable {
		t.Errorf("TextActivity = %+v", text)
	}
	// The signature level is resolved; a level given as a string is unknown.
	if got, ok := m.DeclaredPermissionProtectionLevel("com.dexpp.refs.P_SIGNATURE"); !ok || got != 2 {
		t.Errorf("P_SIGNATURE level = %d, %v", got, ok)
	}
	if got, ok := m.DeclaredPermissionProtectionLevel("com.dexpp.refs.P_TEXT"); ok {
		t.Errorf("P_TEXT level = %d", got)
	}
	if byName["GuardedService"].IsReachable || !byName["TextGuardedService"].IsReachable {
		t.Errorf("GuardedService = %+v, TextGuardedService = %+v", byName["GuardedService"], byName["TextGuardedService"])
	}

	// Values that differ between res/values and res/values-v31.
	if !m.DebuggableVaries || !m.AllowBackupVaries {
		t.Errorf("DebuggableVaries = %v, AllowBackupVaries = %v", m.DebuggableVaries, m.AllowBackupVaries)
	}
	variant := byName["VariantActivity"]
	if variant.Exported == nil || *variant.Exported || !variant.ExportedVaries || !variant.IsExported {
		t.Errorf("VariantActivity = %+v", variant)
	}
	if byName["ExportedActivity"].ExportedVaries {
		t.Error("ExportedActivity varies")
	}
	if got, ok := m.DeclaredPermissionProtectionLevel("com.dexpp.refs.P_VARYING"); !ok || got != 2 ||
		!m.DeclaredPermissionProtectionLevelVaries("com.dexpp.refs.P_VARYING") ||
		m.DeclaredPermissionProtectionLevelVaries("com.dexpp.refs.P_SIGNATURE") ||
		!byName["VaryingLevelService"].IsReachable {
		t.Errorf("P_VARYING level = %d, %v; VaryingLevelService = %+v", got, ok, byName["VaryingLevelService"])
	}

	// Text given by a reference.
	if m.VersionName == nil || *m.VersionName != "1.0" || m.VersionNameReference == nil ||
		*m.VersionNameReference != id("string", "version_name") {
		t.Errorf("VersionName = %v, reference %v", m.VersionName, m.VersionNameReference)
	}
	byReference := byName["PermissionByReferenceService"]
	if byReference.Permission == nil || *byReference.Permission != "com.dexpp.refs.P_SIGNATURE" ||
		byReference.PermissionReference == nil || *byReference.PermissionReference != id("string", "signature_permission") ||
		byReference.IsReachable {
		t.Errorf("PermissionByReferenceService = %+v", byReference)
	}
	// A string with a res/values-fr variant: Android ignores it.
	localized := byName["LocalizedPermissionService"]
	if localized.Permission != nil || localized.PermissionReference == nil || !localized.IsReachable {
		t.Errorf("LocalizedPermissionService = %+v", localized)
	}
	if byName["ExportedActivity"].NameReference != nil {
		t.Error("ExportedActivity has a name reference")
	}

	// sample.apk writes plain values: no references.
	sample := mustManifest(t, mustAPK(t, "sample.apk"))
	if sample.MinSDKReference != nil || sample.DebuggableReference != nil || sample.VersionCodeReference != nil ||
		sample.VersionNameReference != nil || sample.DebuggableVaries || sample.AllowBackupVaries {
		t.Errorf("sample.apk has references: %+v", sample)
	}
	for _, c := range sample.Components {
		if c.ExportedReference != nil || c.PermissionReference != nil || c.NameReference != nil || c.ExportedVaries {
			t.Errorf("%s has a reference: %+v", c.Name, c)
		}
	}
}

func TestResourceValues(t *testing.T) {
	rt := mustAPK(t, "refs.apk").Resources()
	if rt == nil {
		t.Fatal("no resource table")
	}
	id := func(typ, entry string) uint32 {
		t.Helper()
		v, ok := rt.IDOf(typ, entry)
		if !ok {
			t.Fatalf("IDOf(%s, %s) found nothing", typ, entry)
		}
		return v
	}
	values := rt.Values(id("bool", "differs_on_v31"))
	if len(values) != 2 || values[0].Configuration != "" || values[1].Configuration != "v31" ||
		values[0].Type != 0x12 || values[0].Data != 0 || values[0].String != nil {
		t.Fatalf("Values(differs_on_v31) = %+v", values)
	}
	if b, ok := values[0].AsBool(); !ok || b {
		t.Errorf("default AsBool = %v, %v", b, ok)
	}
	if b, ok := values[1].AsBool(); !ok || !b {
		t.Errorf("v31 AsBool = %v, %v", b, ok)
	}
	if n, ok := values[1].AsInt(); !ok || n != -1 {
		t.Errorf("v31 AsInt = %d, %v", n, ok)
	}

	texts := map[string]string{}
	for _, v := range rt.Values(id("string", "greeting")) {
		if v.String == nil {
			t.Fatalf("greeting in %q has no text", v.Configuration)
		}
		texts[v.Configuration] = *v.String
	}
	want := map[string]string{"": "Hello", "fr": "Bonjour", "b+sr+Latn-land-night-v8": "Zdravo"}
	if !reflect.DeepEqual(texts, want) {
		t.Errorf("greeting = %v", texts)
	}

	// A reference is not followed.
	ref := rt.Values(id("bool", "same_as_yes"))
	if len(ref) != 1 || ref[0].Type != 0x01 || ref[0].Data != id("bool", "yes") {
		t.Fatalf("Values(same_as_yes) = %+v", ref)
	}
	if _, ok := ref[0].AsBool(); ok {
		t.Error("a reference reads as a boolean")
	}
	if _, ok := ref[0].AsInt(); ok {
		t.Error("a reference reads as an integer")
	}
	if got := rt.Values(0x7f999999); got != nil {
		t.Errorf("Values(0x7f999999) = %+v", got)
	}
	if b, ok := rt.ResolveBool(id("string", "capital_true")); !ok || b {
		t.Errorf(`ResolveBool("True") = %v, %v`, b, ok)
	}
	if _, ok := rt.ResolveBool(id("string", "empty_text")); ok {
		t.Error(`ResolveBool("") resolved`)
	}
}

func TestResolveBoolAndInt(t *testing.T) {
	rt := mustAPK(t, "refs.apk").Resources()
	if rt == nil {
		t.Fatal("no resource table")
	}
	id := func(typ, entry string) uint32 {
		t.Helper()
		v, ok := rt.IDOf(typ, entry)
		if !ok {
			t.Fatalf("IDOf(%s, %s) found nothing", typ, entry)
		}
		return v
	}
	for entry, want := range map[string]bool{"yes": true, "no": false, "same_as_yes": true} {
		if got, ok := rt.ResolveBool(id("bool", entry)); !ok || got != want {
			t.Errorf("ResolveBool(bool/%s) = %v, %v; want %v", entry, got, ok, want)
		}
	}
	if got, ok := rt.ResolveInt(id("integer", "min_sdk")); !ok || got != 23 {
		t.Errorf("ResolveInt(integer/min_sdk) = %d, %v", got, ok)
	}
	// The string "true" reads as true, as Android reads it.
	if got, ok := rt.ResolveBool(id("string", "not_a_bool")); !ok || !got {
		t.Errorf("ResolveBool(string/not_a_bool) = %v, %v", got, ok)
	}
	if _, ok := rt.ResolveInt(id("string", "not_a_number")); ok {
		t.Error("ResolveInt resolved a string")
	}
	if _, ok := rt.ResolveBool(0x7f999999); ok {
		t.Error("ResolveBool(0x7f999999) resolved")
	}
	if _, ok := rt.ResolveInt(0x7f999999); ok {
		t.Error("ResolveInt(0x7f999999) resolved")
	}
}

func TestAnApkWithoutAManifestOrResources(t *testing.T) {
	// app.apk is a ZIP of DEX files only.
	apk := mustAPK(t, "app.apk")
	if _, ok := apk.Manifest(); ok {
		t.Error("app.apk has a manifest")
	}
	if pkg, ok := apk.Package(); ok {
		t.Errorf("Package = %q", pkg)
	}
	if apk.Resources() != nil {
		t.Error("app.apk has a resource table")
	}
	if _, ok := apk.ResolveString(0x7f010000); ok {
		t.Error("ResolveString found a string without a resource table")
	}
	if apk.Signing().IsSigned() {
		t.Error("app.apk is signed")
	}
}

func TestSigning(t *testing.T) {
	s := mustAPK(t, "signed.apk").Signing()
	if !s.IsSigned() || !s.V1 || !s.V2 || !s.V3 || len(s.Certificates) != 1 {
		t.Fatalf("Signing = %+v", s)
	}
	c := s.Certificates[0]
	if c.Subject != "C=MA, O=DexppOrg, CN=Dexpp Test" || c.Issuer != c.Subject || !c.SelfSigned {
		t.Errorf("subject %q, issuer %q", c.Subject, c.Issuer)
	}
	if c.SerialHex != "ba51e610667560a3" {
		t.Errorf("SerialHex = %q", c.SerialHex)
	}
	if c.SHA256Hex != "6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599" {
		t.Errorf("SHA256Hex = %q", c.SHA256Hex)
	}
	if len(c.SHA1Hex) != 40 || len(c.DER) == 0 || c.DER[0] != 0x30 {
		t.Errorf("SHA1Hex = %q, %d DER bytes", c.SHA1Hex, len(c.DER))
	}
	if len(c.NotBefore) != len("2026-01-01T00:00:00Z") || c.NotBefore >= c.NotAfter {
		t.Errorf("validity %q to %q", c.NotBefore, c.NotAfter)
	}

	unsigned := mustAPK(t, "sample.apk").Signing()
	if unsigned.IsSigned() || unsigned.Certificates != nil {
		t.Errorf("sample.apk signing = %+v", unsigned)
	}
}

func TestApkAnalysis(t *testing.T) {
	apk := mustAPK(t, "sample.apk")
	ctx, err := apk.Analysis()
	if err != nil {
		t.Fatal(err)
	}
	defer ctx.Close()
	apk.Close() // the context stays valid
	if ctx.FindClass("LTestDex;") == nil {
		t.Error("no LTestDex; in sample.apk")
	}

	_, err = mustAPK(t, "nodex.apk").Analysis()
	if !errors.Is(err, ErrInvalidAPK) {
		t.Errorf("nodex.apk analysis: %v", err)
	}
}

func TestOpenAPKBytesMatchesOpenAPK(t *testing.T) {
	raw, err := os.ReadFile(data("signed.apk"))
	if err != nil {
		t.Fatal(err)
	}
	fromFile := mustAPK(t, "signed.apk")
	apk, err := OpenAPKBytes(raw, "signed.apk")
	if err != nil {
		t.Fatalf("OpenAPKBytes: %v", err)
	}
	defer apk.Close()
	// The Apk holds its own copy: overwriting the slice changes nothing.
	clear(raw)

	if got, want := apk.Entries(), fromFile.Entries(); !slices.Equal(got, want) {
		t.Errorf("Entries = %v, want %v", got, want)
	}
	dex, ok := apk.Read("classes.dex")
	want, _ := fromFile.Read("classes.dex")
	if !ok || !bytes.Equal(dex, want) {
		t.Errorf("Read(classes.dex) = %d bytes, %v; want %d bytes", len(dex), ok, len(want))
	}
	if pkg, ok := apk.Package(); !ok || pkg != "com.example.dexpp" {
		t.Errorf("Package = %q, %v", pkg, ok)
	}
	if name, ok := apk.Resources().ResolveString(0x7f010000); !ok || name != "Dexpp Sample" {
		t.Errorf("ResolveString = %q, %v", name, ok)
	}
	s := apk.Signing()
	if !s.IsSigned() || len(s.Certificates) != 1 ||
		s.Certificates[0].SHA256Hex != fromFile.Signing().Certificates[0].SHA256Hex {
		t.Errorf("Signing = %+v", s)
	}
	ctx, err := apk.Analysis(WithThreads(1))
	if err != nil {
		t.Fatalf("Analysis: %v", err)
	}
	defer ctx.Close()
	if ctx.FindClass("LTestDex;") == nil {
		t.Error("no LTestDex; in the bytes of signed.apk")
	}
}

func TestOpenAPKBytesErrors(t *testing.T) {
	for _, c := range []struct {
		data   []byte
		label  string
		prefix string
	}{
		{[]byte("not an apk"), "", "<apk>: "},
		{[]byte("not an apk"), "upload.apk", "upload.apk: "},
		{nil, "", "<apk>: "},
		{[]byte{}, "empty.apk", "empty.apk: "},
	} {
		apk, err := OpenAPKBytes(c.data, c.label)
		if apk != nil || !errors.Is(err, ErrInvalidAPK) {
			t.Errorf("OpenAPKBytes(%q, %q) = %v, %v", c.data, c.label, apk, err)
			continue
		}
		var e *Error
		if !errors.As(err, &e) || !strings.HasPrefix(e.Message, c.prefix) {
			t.Errorf("OpenAPKBytes(%q, %q): message %q, want prefix %q", c.data, c.label, err, c.prefix)
		}
	}

	// The label also starts the errors of Analysis.
	raw, err := os.ReadFile(data("nodex.apk"))
	if err != nil {
		t.Fatal(err)
	}
	apk, err := OpenAPKBytes(raw, "nodex-upload.apk")
	if err != nil {
		t.Fatalf("OpenAPKBytes: %v", err)
	}
	defer apk.Close()
	_, err = apk.Analysis()
	var e *Error
	if !errors.Is(err, ErrInvalidAPK) || !errors.As(err, &e) ||
		!strings.HasPrefix(e.Message, "nodex-upload.apk: ") {
		t.Errorf("Analysis of nodex.apk bytes: %v", err)
	}
}

func TestApkAnalysisWithAnyThreadCount(t *testing.T) {
	// multidex.apk has four deflated DEX entries, enough for the parallel path.
	apk := mustAPK(t, "multidex.apk")
	load := func(opts ...LoadOption) []string {
		ctx, err := apk.Analysis(opts...)
		if err != nil {
			t.Fatalf("Analysis: %v", err)
		}
		defer ctx.Close()
		return classDescriptors(ctx)
	}
	serial := load(WithThreads(1))
	if len(serial) == 0 || serial[0] != "LTestDex;" {
		t.Fatalf("serial classes = %v, want LTestDex; first", serial)
	}
	for _, opts := range [][]LoadOption{{WithThreads(4)}, nil, {WithThreads(-3)}, {nil}, {WithThreads(1)}} {
		if got := load(opts...); !slices.Equal(got, serial) {
			t.Errorf("classes with %d options = %v, want %v", len(opts), got, serial)
		}
	}
}

func TestApkReadsAnEmptyEntry(t *testing.T) {
	// A one-entry ZIP whose entry is empty: Read gives an empty slice and
	// true, not nil and false.
	var buf bytes.Buffer
	w := zip.NewWriter(&buf)
	if _, err := w.CreateRaw(&zip.FileHeader{Name: "e", Method: zip.Store}); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), "empty-entry.zip")
	if err := os.WriteFile(path, buf.Bytes(), 0o644); err != nil {
		t.Fatal(err)
	}
	apk, err := OpenAPK(path)
	if err != nil {
		t.Fatalf("OpenAPK: %v", err)
	}
	defer apk.Close()
	if got := apk.Entries(); !slices.Equal(got, []string{"e"}) {
		t.Fatalf("Entries = %v", got)
	}
	if got, ok := apk.Read("e"); !ok || got == nil || len(got) != 0 {
		t.Errorf("Read(e) = %v, %v", got, ok)
	}
}
