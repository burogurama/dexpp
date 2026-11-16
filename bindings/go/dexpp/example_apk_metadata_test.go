package dexpp_test

// The testable examples of the guide "APK metadata". docs_examples_test.go says
// how the guide uses them. Code outside the markers is not shown: it sets up
// the variables that the earlier sections of the guide made.

import (
	"errors"
	"fmt"
	"log"
	"os"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// openManifest opens an APK and returns it with its manifest.
func openManifest(path string) (*dexpp.Apk, dexpp.Manifest) {
	apk, err := dexpp.OpenAPK(path)
	if err != nil {
		log.Fatal(err)
	}
	manifest, ok := apk.Manifest()
	if !ok {
		log.Fatal("no manifest in ", path)
	}
	return apk, manifest
}

// Read the manifest of an APK.
func ExampleApk_Manifest() {
	// --8<-- [start:manifest]
	apk, err := dexpp.OpenAPK("../../../tests/data/sample.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer apk.Close()
	manifest, ok := apk.Manifest()
	if !ok {
		log.Fatal("no manifest")
	}

	fmt.Println(manifest.Package)
	// An attribute the manifest may leave out is a pointer, nil when it is not set.
	if manifest.VersionCode != nil && manifest.VersionName != nil {
		fmt.Println("version:", *manifest.VersionCode, *manifest.VersionName)
	}
	if manifest.MinSDK != nil && manifest.TargetSDK != nil {
		fmt.Println("sdk:", *manifest.MinSDK, "to", *manifest.TargetSDK)
	}
	fmt.Println("uses:", manifest.Permissions)
	for _, permission := range manifest.DeclaredPermissions {
		fmt.Println("declares:", permission.Name)
	}
	if manifest.Debuggable != nil && manifest.AllowBackup != nil {
		fmt.Println("debuggable:", *manifest.Debuggable, "allow backup:", *manifest.AllowBackup)
	}
	// --8<-- [end:manifest]

	// Output:
	// com.example.dexpp
	// version: 7 1.2.3
	// sdk: 21 to 34
	// uses: [android.permission.INTERNET android.permission.CAMERA]
	// declares: com.example.dexpp.CUSTOM
	// debuggable: true allow backup: false
}

// List the components of an APK and whether they are exported.
func ExampleManifest_ComponentsOf() {
	apk, manifest := openManifest("../../../tests/data/sample.apk")
	defer apk.Close()

	// --8<-- [start:components]
	for _, component := range manifest.Components {
		state := "private"
		if component.IsExported {
			state = "exported"
		}
		fmt.Println(component.Kind, component.Name, state)
	}

	activities := manifest.ComponentsOf(dexpp.ComponentActivity)
	launcher := activities[0]
	fmt.Println(launcher.IntentFilters[0].Actions)
	// --8<-- [end:components]

	// Output:
	// activity com.example.dexpp.MainActivity exported
	// service com.example.dexpp.SyncService private
	// receiver com.example.dexpp.BootReceiver exported
	// provider com.example.dexpp.DataProvider private
	// [android.intent.action.MAIN]
}

// Find the exported components that another app still cannot reach.
func ExampleComponent_reachable() {
	// --8<-- [start:reachable]
	guardedAPK, err := dexpp.OpenAPK("../../../tests/data/reachable.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer guardedAPK.Close()
	guarded, ok := guardedAPK.Manifest()
	if !ok {
		log.Fatal("no manifest")
	}

	reachable := 0
	for _, component := range guarded.Components {
		if component.IsReachable {
			reachable++
		} else if component.IsExported && component.Permission != nil {
			permission := *component.Permission
			level, _ := guarded.DeclaredPermissionProtectionLevel(permission)
			fmt.Printf("%s is exported, but guarded by %s (level %d)\n", component.Name, permission, level)
		}
	}
	fmt.Println(reachable, "of", len(guarded.Components), "components are reachable")
	// --8<-- [end:reachable]

	// Output:
	// com.dexpp.reach.SigActivity is exported, but guarded by com.dexpp.reach.P_SIGNATURE (level 2)
	// com.dexpp.reach.SigService is exported, but guarded by com.dexpp.reach.P_SIGNATURE (level 2)
	// 5 of 8 components are reachable
}

// Look up a resource by name, and resolve its value.
func ExampleApk_Resources() {
	apk, _ := openManifest("../../../tests/data/sample.apk")
	defer apk.Close()

	// --8<-- [start:resources]
	table := apk.Resources()
	if table == nil {
		log.Fatal("no resource table")
	}

	appName, ok := table.IDOf("string", "app_name")
	if !ok {
		log.Fatal("no @string/app_name")
	}
	name, ok := table.NameOf(appName)
	if !ok {
		log.Fatal("no name for ", appName)
	}

	fmt.Printf("%#x %s %s %s\n", appName, name.Package, name.Type, name.Entry)
	fmt.Println(table.ResolveString(appName))
	fmt.Println(apk.ResolveString(appName))
	// --8<-- [end:resources]

	// Output:
	// 0x7f010000 com.example.dexpp string app_name
	// Dexpp Sample true
	// Dexpp Sample true
}

// Read the signature schemes and the certificates of a signed APK.
func ExampleApk_Signing() {
	// --8<-- [start:signing]
	signedAPK, err := dexpp.OpenAPK("../../../tests/data/signed.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer signedAPK.Close()
	signing := signedAPK.Signing()

	fmt.Println("signed:", signing.IsSigned())
	fmt.Println("v1:", signing.V1, "v2:", signing.V2, "v3:", signing.V3)
	for _, certificate := range signing.Certificates {
		fmt.Println(certificate.Subject)
		fmt.Println("valid", certificate.NotBefore, "to", certificate.NotAfter)
		fmt.Println("sha256", certificate.SHA256Hex)
	}
	// --8<-- [end:signing]

	// Output:
	// signed: true
	// v1: true v2: true v3: true
	// C=MA, O=DexppOrg, CN=Dexpp Test
	// valid 2026-06-11T13:54:30Z to 2053-10-27T13:54:30Z
	// sha256 6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599
}

// List the entries of the archive, and read one.
func ExampleApk_Entries() {
	apk, _ := openManifest("../../../tests/data/sample.apk")
	defer apk.Close()

	// --8<-- [start:entries]
	entries := apk.Entries()
	fmt.Println(entries)

	classesDEX, ok := apk.Read("classes.dex")
	if !ok {
		log.Fatal("no classes.dex")
	}
	fmt.Println(len(classesDEX), "bytes of classes.dex")
	// --8<-- [end:entries]

	// Output:
	// [AndroidManifest.xml resources.arsc classes.dex]
	// 980 bytes of classes.dex
}

// Open an APK from its bytes in memory, and load its code on one thread.
func ExampleOpenAPKBytes() {
	// --8<-- [start:memory]
	// The bytes can come from anywhere: a download, an upload, a database.
	data, err := os.ReadFile("../../../tests/data/sample.apk")
	if err != nil {
		log.Fatal(err)
	}

	inMemory, err := dexpp.OpenAPKBytes(data, "sample.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer inMemory.Close()
	if pkg, ok := inMemory.Package(); ok {
		fmt.Println(pkg)
	}

	// WithThreads(1) keeps all the work on the calling thread.
	code, err := inMemory.Analysis(dexpp.WithThreads(1))
	if err != nil {
		log.Fatal(err)
	}
	defer code.Close()
	var descriptors []string
	for _, class := range code.Classes() {
		descriptors = append(descriptors, class.Descriptor())
	}
	fmt.Println("classes:", descriptors)

	// The label starts the error messages, where OpenAPK puts the path.
	_, err = dexpp.OpenAPKBytes([]byte("not an apk"), "upload.apk")
	if errors.Is(err, dexpp.ErrInvalidAPK) {
		fmt.Println("ErrInvalidAPK:", err)
	}
	// --8<-- [end:memory]

	// Output:
	// com.example.dexpp
	// classes: [LTestDex;]
	// ErrInvalidAPK: upload.apk: buffer too small to be a ZIP archive
}
