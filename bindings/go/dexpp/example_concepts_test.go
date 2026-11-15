package dexpp_test

// The testable examples of the concepts page. docs_examples_test.go says how
// the page uses them.

import (
	"fmt"
	"log"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// A class handle keeps the analysis alive after its context is closed.
func ExampleContext_Close() {
	// --8<-- [start:handles]
	// mainClass loads a DEX file and returns one of its classes.
	mainClass := func(path string) *dexpp.Class {
		ctx, err := dexpp.FromDEX(path)
		if err != nil {
			log.Fatal(err)
		}
		defer ctx.Close() // the context is freed here
		return ctx.FindClass("LTestDex;")
	}

	cls := mainClass("../../../tests/data/classes.dex")
	if cls == nil {
		log.Fatal("no TestDex class")
	}
	var methodNames []string
	for _, method := range cls.Methods() { // still works
		methodNames = append(methodNames, method.Name())
	}
	fmt.Println(cls.Descriptor(), methodNames)
	// --8<-- [end:handles]

	// Output:
	// LTestDex; [<init> helloDex main]
}
