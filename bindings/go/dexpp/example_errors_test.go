package dexpp_test

// The testable examples about errors, for the quick start, the concepts page
// and the Go reference page. docs_examples_test.go says how the pages use them.

import (
	"errors"
	"fmt"
	"io/fs"
	"log"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// Tell a missing file and a file that is not an APK apart.
func ExampleOpenAPK() {
	// --8<-- [start:load]
	paths := []string{
		"../../../tests/data/sample.apk",
		"../../../tests/data/missing.apk",
		"../../../tests/data/classes.dex",
	}
	for _, path := range paths {
		apk, err := dexpp.OpenAPK(path)
		switch {
		case errors.Is(err, fs.ErrNotExist):
			fmt.Println(path, "does not exist")
		case errors.Is(err, dexpp.ErrInvalidAPK):
			fmt.Println("not an APK:", err)
		case err != nil:
			log.Fatal(err)
		default:
			name, _ := apk.Package()
			fmt.Println(name)
			apk.Close()
		}
	}
	// --8<-- [end:load]

	// Output:
	// com.example.dexpp
	// ../../../tests/data/missing.apk does not exist
	// not an APK: ../../../tests/data/classes.dex: no end-of-central-directory record (not a ZIP archive)
}

// Every failed call returns a *dexpp.Error, which carries an error code.
func ExampleError() {
	// --8<-- [start:base]
	_, err := dexpp.FromDEX("../../../tests/data/TestDex.java")
	var dexppErr *dexpp.Error
	if errors.As(err, &dexppErr) {
		fmt.Println(dexppErr.Code, "-", dexppErr.Message)
	}
	// --8<-- [end:base]

	// Output:
	// invalid DEX - ../../../tests/data/TestDex.java: Invalid DEX magic number
}
