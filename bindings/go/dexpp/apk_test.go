package dexpp

import (
	"archive/zip"
	"bytes"
	"errors"
	"os"
	"path/filepath"
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
	// Exported and unguarded, or guarded by a normal, dangerous or undeclared
	// permission: reachable.
	for _, name := range []string{"OpenActivity", "NormalActivity", "DangerousActivity",
		"PlatformPermActivity", "OpenReceiver"} {
		if !byName[name].IsReachable {
			t.Errorf("%s is not reachable", name)
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
	if exported != 5 || reachable != 4 {
		t.Errorf("%d exported and %d reachable activities, want 5 and 4", exported, reachable)
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
	// A reference to a string: written, but unknown, so possibly exported.
	text := byName["TextActivity"]
	if text.Exported != nil || text.ExportedReference == nil || *text.ExportedReference != id("string", "not_a_bool") ||
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

	// sample.apk writes plain values: no references.
	sample := mustManifest(t, mustAPK(t, "sample.apk"))
	if sample.MinSDKReference != nil || sample.DebuggableReference != nil || sample.VersionCodeReference != nil {
		t.Errorf("sample.apk has references: %+v", sample)
	}
	for _, c := range sample.Components {
		if c.ExportedReference != nil {
			t.Errorf("%s has an exported reference", c.Name)
		}
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
	if _, ok := rt.ResolveBool(id("string", "not_a_bool")); ok {
		t.Error("ResolveBool resolved a string")
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
