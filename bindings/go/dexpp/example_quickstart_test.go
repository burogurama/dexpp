package dexpp_test

// The testable examples of the documentation's home page. docs_examples_test.go
// says how the pages use them.

import (
	"fmt"
	"log"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// Open an APK, read its metadata, then look at its code.
func ExampleApk_Analysis() {
	// --8<-- [start:quickstart]
	apk, err := dexpp.OpenAPK("../../../tests/data/signed.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer apk.Close()

	// Package metadata comes from the manifest. ok is false when the APK has none.
	manifest, ok := apk.Manifest()
	if !ok {
		log.Fatal("no manifest")
	}
	fmt.Println("package:", manifest.Package)
	if manifest.VersionName != nil {
		fmt.Println("version:", *manifest.VersionName)
	}
	fmt.Println("permissions:", manifest.Permissions)
	if manifest.LauncherActivity != nil {
		fmt.Println("launcher:", *manifest.LauncherActivity)
	}

	// Who signed it.
	for _, certificate := range apk.Signing().Certificates {
		fmt.Println("signed by:", certificate.Subject, certificate.SHA256Hex[:16]+"...")
	}

	// The code: every classes*.dex of the APK in one analysis context.
	ctx, err := apk.Analysis()
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()
	for _, cls := range ctx.Classes() {
		var methodNames []string
		for _, method := range cls.Methods() {
			methodNames = append(methodNames, method.Name())
		}
		fmt.Println(cls.Descriptor(), methodNames)
	}
	// --8<-- [end:quickstart]

	// Output:
	// package: com.example.dexpp
	// version: 1.2.3
	// permissions: [android.permission.INTERNET android.permission.CAMERA]
	// launcher: com.example.dexpp.MainActivity
	// signed by: C=MA, O=DexppOrg, CN=Dexpp Test 6c9647205d1ae141...
	// LTestDex; [<init> helloDex main]
}
