package dexpp_test

// The testable examples of the guide "Class hierarchy and cross-references".
// docs_examples_test.go says how the guide uses them. Code outside the markers
// is not shown: it sets up the variables that the earlier sections of the guide
// made.

import (
	"fmt"
	"log"
	"regexp"
	"sort"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// loadXrefs loads a DEX file and builds its cross-reference index.
func loadXrefs(path string) (*dexpp.Context, *dexpp.Xrefs) {
	ctx, err := dexpp.FromDEX(path)
	if err != nil {
		log.Fatal(err)
	}
	xrefs, err := ctx.Xrefs()
	if err != nil {
		log.Fatal(err)
	}
	return ctx, xrefs
}

// Ask the class hierarchy about inheritance.
func ExampleContext_ClassHierarchy() {
	// --8<-- [start:hierarchy]
	shapes, err := dexpp.FromDEX("../../../tests/data/shapes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer shapes.Close()
	hierarchy, err := shapes.ClassHierarchy()
	if err != nil {
		log.Fatal(err)
	}

	supertypes := hierarchy.Supertypes("LCircle;")
	ancestors := hierarchy.AllSupertypes("LCircle;")
	descendants := hierarchy.AllDescendants("LShape;")
	sort.Strings(descendants)
	fmt.Println(supertypes)
	fmt.Println(ancestors)
	fmt.Println(descendants)
	fmt.Println(hierarchy.Implementers("LShape;"))
	subclasses := hierarchy.Subclasses("LAbstractShape;")
	sort.Strings(subclasses)
	fmt.Println(subclasses)

	isShape := hierarchy.IsSubtypeOf("LCircle;", "LShape;")
	fmt.Println(isShape, hierarchy.IsInterface("LShape;"))
	fmt.Println(hierarchy.IsLoaded("Ljava/lang/Object;"))
	// --8<-- [end:hierarchy]

	// Output:
	// [LAbstractShape;]
	// [LAbstractShape; Ljava/lang/Object; LShape;]
	// [LAbstractShape; LCircle; LRectangle;]
	// [LAbstractShape;]
	// [LCircle; LRectangle;]
	// true true
	// false
}

// Find every call to a method name.
func ExampleXrefs_MethodRefsByName() {
	// --8<-- [start:method_refs]
	ctx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()
	xrefs, err := ctx.Xrefs()
	if err != nil {
		log.Fatal(err)
	}

	for _, group := range xrefs.MethodRefsByName("println") {
		for _, site := range group.Sites {
			caller, _ := xrefs.ReferrerID(site.Referrer)
			called := group.Target.ClassDescriptor + " " + group.Target.Name
			fmt.Println(called, "is called by", caller.Name, "at", site.CodeOffset)
		}
	}
	// --8<-- [end:method_refs]

	// Output:
	// Ljava/io/PrintStream; println is called by main at 6
}

// Find the methods that load a string.
func ExampleXrefs_StringRefs() {
	ctx, xrefs := loadXrefs("../../../tests/data/classes.dex")
	defer ctx.Close()

	// --8<-- [start:strings]
	loaded := xrefs.ReferencedStrings()
	fmt.Println(loaded)

	for _, site := range xrefs.StringRefs("Hello Dex++!!") {
		method := xrefs.ReferrerMethod(site.Referrer)
		if method == nil {
			log.Fatal("no method for referrer ", site.Referrer)
		}
		fmt.Println("loaded in", method.DeclaringClass().Descriptor(), method.Name())
	}
	// --8<-- [end:strings]

	// Output:
	// [Hello Dex++!!]
	// loaded in LTestDex; helloDex
}

// Search the loaded strings with a pattern.
func ExampleXrefs_ReferencedStrings() {
	// --8<-- [start:pattern]
	greetingsCtx, err := dexpp.FromDEX("../../../tests/data/multistring.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer greetingsCtx.Close()
	greetings, err := greetingsCtx.Xrefs()
	if err != nil {
		log.Fatal(err)
	}

	world := regexp.MustCompile(`World!$`)
	values := greetings.ReferencedStrings() // in no fixed order
	sort.Strings(values)
	for _, value := range values {
		if world.MatchString(value) {
			for _, site := range greetings.StringRefs(value) {
				loader, _ := greetings.ReferrerID(site.Referrer)
				fmt.Printf("%q loaded by %s at %d\n", value, loader.Name, site.CodeOffset)
			}
		}
	}
	// --8<-- [end:pattern]

	// Output:
	// "Goodbye, World!" loaded by <clinit> at 4
	// "Hello, World!" loaded by <clinit> at 0
}

// Count the reads and writes of a field.
func ExampleXrefs_FieldReads() {
	ctx, xrefs := loadXrefs("../../../tests/data/classes.dex")
	defer ctx.Close()

	// --8<-- [start:fields]
	out := dexpp.FieldID{
		ClassDescriptor: "Ljava/lang/System;", Name: "out", Type: "Ljava/io/PrintStream;",
	}
	reads := xrefs.FieldReads(out)
	writes := xrefs.FieldWrites(out)
	fmt.Println(len(reads), "reads,", len(writes), "writes")
	// --8<-- [end:fields]

	// Output:
	// 1 reads, 0 writes
}

// Find the instructions that use a type.
func ExampleXrefs_TypeRefs() {
	// --8<-- [start:types]
	petsCtx, err := dexpp.FromDEX("../../../tests/data/polymorphic.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer petsCtx.Close()
	pets, err := petsCtx.Xrefs()
	if err != nil {
		log.Fatal(err)
	}

	for _, site := range pets.TypeRefs("Ljava/lang/StringBuilder;") {
		user, _ := pets.ReferrerID(site.Referrer)
		fmt.Println(site.Kind, "in", user.ClassDescriptor, user.Name, "at", site.CodeOffset)
	}
	// --8<-- [end:types]

	// Output:
	// new-instance in LPet; describe at 0
	// new-instance in LDog; describe at 0
}
