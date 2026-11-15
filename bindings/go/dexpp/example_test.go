package dexpp_test

// Testable examples. go test runs each one and compares what it prints with
// its "Output:" comment. docs/c-go.md shows the code between the
// "--8<-- [start:name]" and "--8<-- [end:name]" markers, so the Go code in the
// documentation is checked too.
//
// go test runs the examples in bindings/go/dexpp, so they open the test files
// of the repository by the relative path ../../../tests/data.

import (
	"errors"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"sort"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// Load a DEX file and list its classes and methods.
func ExampleFromDEX() {
	// --8<-- [start:load]
	ctx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	for _, cls := range ctx.Classes() {
		fmt.Println(cls.Descriptor())
		for _, m := range cls.Methods() {
			fmt.Printf("  %s%s  %v\n", m.Name(), m.Proto(), m.AccessFlags())
		}
	}
	if ctx.FindClass("Lcom/example/Missing;") == nil {
		fmt.Println("no such class")
	}
	// --8<-- [end:load]

	// Output:
	// LTestDex;
	//   <init>()V  public|constructor
	//   helloDex()Ljava/lang/String;  public|static
	//   main([Ljava/lang/String;)V  public|static
	// no such class
}

// Load every DEX file of an APK, and cross from the package to its code.
func ExampleOpenAPK() {
	// --8<-- [start:apk]
	apk, err := dexpp.OpenAPK("../../../tests/data/sample.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer apk.Close()

	if m, ok := apk.Manifest(); ok {
		fmt.Println("package:", m.Package)
		if m.MinSDK != nil {
			fmt.Println("min SDK:", *m.MinSDK)
		}
		for _, c := range m.Components {
			fmt.Printf("%-8s %s exported=%v reachable=%v\n", c.Kind, c.Name, c.IsExported, c.IsReachable)
		}
	}
	if rt := apk.Resources(); rt != nil {
		if id, ok := rt.IDOf("string", "app_name"); ok {
			name, _ := rt.ResolveString(id)
			fmt.Printf("@string/app_name = %q\n", name)
		}
	}
	fmt.Println("signed:", apk.Signing().IsSigned())

	ctx, err := apk.Analysis() // the code: every classes*.dex of the APK
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()
	fmt.Println("classes:", len(ctx.Classes()))
	// --8<-- [end:apk]

	// Output:
	// package: com.example.dexpp
	// min SDK: 21
	// activity com.example.dexpp.MainActivity exported=true reachable=true
	// service  com.example.dexpp.SyncService exported=false reachable=false
	// receiver com.example.dexpp.BootReceiver exported=true reachable=true
	// provider com.example.dexpp.DataProvider exported=false reachable=false
	// @string/app_name = "Dexpp Sample"
	// signed: false
	// classes: 1
}

// Read the certificates of a signed APK.
func ExampleApk_Signing() {
	apk, err := dexpp.OpenAPK("../../../tests/data/signed.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer apk.Close()

	// --8<-- [start:signing]
	s := apk.Signing()
	fmt.Println("schemes: v1", s.V1, "v2", s.V2, "v3", s.V3)
	for _, c := range s.Certificates {
		fmt.Println(c.Subject)
		fmt.Println("sha256:", c.SHA256Hex)
	}
	// --8<-- [end:signing]

	// Output:
	// schemes: v1 true v2 true v3 true
	// C=MA, O=DexppOrg, CN=Dexpp Test
	// sha256: 6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599
}

// Disassemble a method, and tell instruction kinds apart with a type switch.
func ExampleMethod_Instructions() {
	ctx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:instructions]
	for _, m := range ctx.FindClass("LTestDex;").Methods() {
		if m.Name() != "main" {
			continue
		}
		for _, insn := range m.Instructions() {
			fmt.Printf("%04x  %s\n", insn.Base().Offset, dexpp.ToString(insn))
			switch insn := insn.(type) {
			case dexpp.InvokeInstruction:
				fmt.Println("      call, arguments in", insn.ArgRegs)
			case dexpp.FieldInstruction:
				fmt.Println("      field read, static:", insn.IsStatic)
			}
		}
	}
	// --8<-- [end:instructions]

	// Output:
	// 0000  sget-object v1, field@0
	//       field read, static: true
	// 0002  invoke-static method@1 {}
	//       call, arguments in []
	// 0005  move-result-object v0
	// 0006  invoke-virtual method@3 {v1, v0}
	//       call, arguments in [1 0]
	// 0009  return-void
}

// Walk the control-flow graph of a method.
func ExampleMethod_Cfg() {
	ctx, err := dexpp.FromDEX("../../../tests/data/trycatch.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:cfg]
	for _, m := range ctx.FindClass("LTryCatch;").Methods() {
		if m.Name() != "divide" {
			continue
		}
		for _, b := range m.Cfg().Blocks {
			fmt.Printf("block %d [%d, %d)", b.Index, b.StartOffset, b.EndOffset)
			for _, e := range b.Successors {
				fmt.Printf(" -> %d (%v)", e.TargetBlock, e.Kind)
			}
			fmt.Println()
		}
		for _, t := range m.TryBlocks() {
			for _, h := range t.Handlers {
				fmt.Printf("try [%d, %d) catches %s at %d\n", t.StartOffset, t.EndOffset,
					h.TypeDescriptor, h.HandlerOffset)
			}
		}
	}
	// --8<-- [end:cfg]

	// Output:
	// block 0 [0, 1) -> 1 (fallthrough) -> 2 (exception)
	// block 1 [1, 2)
	// block 2 [2, 5)
	// try [0, 1) catches Ljava/lang/ArithmeticException; at 2
}

// List what a method calls, reads and loads, with names instead of indices.
func ExampleMethod_Calls() {
	ctx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:references]
	for _, m := range ctx.FindClass("LTestDex;").Methods() {
		for _, c := range m.Calls() {
			fmt.Println(m.Name(), "calls", c.Target, "with", c.Kind)
		}
		for _, f := range m.FieldAccesses() {
			fmt.Println(m.Name(), "reads", f.Field)
		}
		for _, s := range m.StringLoads() {
			fmt.Printf("%s loads %q\n", m.Name(), s.Value)
		}
	}
	// --8<-- [end:references]

	// Output:
	// <init> calls Ljava/lang/Object;-><init>()V with direct
	// helloDex loads "Hello Dex++!!"
	// main calls LTestDex;->helloDex()Ljava/lang/String; with static
	// main calls Ljava/io/PrintStream;->println(Ljava/lang/String;)V with virtual
	// main reads Ljava/lang/System;->out:Ljava/io/PrintStream;
}

// Read annotations and the initial values of static fields.
func ExampleClass_Annotations() {
	ctx, err := dexpp.FromDEX("../../../tests/data/annotated.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:annotations]
	cls := ctx.FindClass("LAnnotated;")
	for _, a := range cls.Annotations() {
		fmt.Println(a.TypeDescriptor, a.Visibility)
		for _, e := range a.Elements {
			fmt.Printf("  %s = %v (%T)\n", e.Name, e.Value, e.Value)
		}
	}
	for _, f := range cls.Fields() {
		if v, ok := f.InitialValue(); ok {
			fmt.Printf("%s starts at %v\n", f.Name(), v)
		}
	}
	// --8<-- [end:annotations]

	// Output:
	// LMarker; runtime
	//   level = 3 (int64)
	//   name = cls (string)
	//   tags = [a b] ([]interface {})
	// BIG starts at 1234567890123
	// FLAG starts at true
	// GREETING starts at hello
	// MAGIC starts at 42
	// PI starts at 3.25
}

// Ask the call graph who may call a method, through virtual dispatch too.
func ExampleCallGraph() {
	ctx, err := dexpp.FromDEX("../../../tests/data/polymorphic.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:callgraph]
	g, err := ctx.CallGraph()
	if err != nil {
		log.Fatal(err)
	}
	sound, ok := g.Find(dexpp.MethodID{
		ClassDescriptor: "LAnimal;", Name: "sound", Proto: "()Ljava/lang/String;",
	})
	if !ok {
		log.Fatal("no Animal.sound")
	}
	// A call to Animal.sound may run these overrides:
	for _, i := range g.DispatchTargets(sound.Index) {
		target, _ := g.Node(i)
		fmt.Println("dispatches to", target.ID)
	}
	// The call sites that name Animal.sound:
	for _, i := range g.InEdges(sound.Index) {
		e, _ := g.Edge(i)
		caller, _ := g.Node(e.CallerNode)
		fmt.Println("called by", caller.ID.ClassDescriptor+"."+caller.ID.Name, e.Kind, e.Origin)
	}
	// --8<-- [end:callgraph]

	// Output:
	// dispatches to LCat;->sound()Ljava/lang/String;
	// dispatches to LDog;->sound()Ljava/lang/String;
	// called by LKennel;.noise interface declared
	// called by LPet;.describe virtual inherited-resolution
}

// Query the class hierarchy and the cross-reference index.
func ExampleXrefs() {
	ctx, err := dexpp.FromDEX("../../../tests/data/polymorphic.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	// --8<-- [start:hierarchy]
	h, err := ctx.ClassHierarchy()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(h.AllSupertypes("LDog;"))
	fmt.Println(h.IsSubtypeOf("LDog;", "LAnimal;"))

	x, err := ctx.Xrefs()
	if err != nil {
		log.Fatal(err)
	}
	groups := x.MethodRefsByName("sound") // in no fixed order
	sort.Slice(groups, func(i, j int) bool {
		return groups[i].Target.String() < groups[j].Target.String()
	})
	for _, group := range groups {
		for _, site := range group.Sites {
			caller, _ := x.ReferrerID(site.Referrer)
			fmt.Println(caller.Name, "calls", group.Target.ClassDescriptor, "at", site.CodeOffset)
		}
	}
	// --8<-- [end:hierarchy]

	// Output:
	// [LPet; Ljava/lang/Object; LAnimal;]
	// true
	// noise calls LAnimal; at 0
	// dogNoise calls LDog; at 0
	// describe calls LPet; at 15
}

// Split a DEX file into one file per class, with the editable model.
func ExampleModel() {
	dir, err := os.MkdirTemp("", "dexpp-example")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(dir)

	// --8<-- [start:model]
	m, err := dexpp.ReadModelFile("../../../tests/data/shapes.dex", true)
	if err != nil {
		log.Fatal(err)
	}
	defer m.Close()

	for i, desc := range m.ClassDescriptors() {
		one := dexpp.NewModel(m.Version())
		if err := one.CopyClass(m, desc); err != nil {
			log.Fatal(err)
		}
		if err := one.WriteFile(filepath.Join(dir, fmt.Sprintf("class%d.dex", i)), true); err != nil {
			log.Fatal(err)
		}
		one.Close()
		fmt.Println("wrote", desc)
	}

	// Rename a class everywhere it is referenced, then check and write.
	m.RenameTypes(map[string]string{"LCircle;": "Lcom/example/Round;"})
	if problems := m.Validate(); len(problems) == 0 {
		out, err := m.Write(true)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Println("renamed:", m.HasClass("Lcom/example/Round;"), len(out) > 0)
	}
	// --8<-- [end:model]

	// Output:
	// wrote LShape;
	// wrote LAbstractShape;
	// wrote LCircle;
	// wrote LRectangle;
	// renamed: true true
}

// Tell errors apart with errors.Is and errors.As.
func ExampleError() {
	// --8<-- [start:errors]
	_, err := dexpp.FromDEX("missing.dex")
	fmt.Println(errors.Is(err, fs.ErrNotExist), errors.Is(err, dexpp.ErrFileNotFound))

	_, err = dexpp.FromDEX("../../../tests/data/TestDex.java")
	fmt.Println(errors.Is(err, dexpp.ErrInvalidDex))

	m, err := dexpp.ReadModelFile("../../../tests/data/shapes.dex", true)
	if err != nil {
		log.Fatal(err)
	}
	defer m.Close()
	m.RenameTypes(map[string]string{"LCircle;": "not a descriptor"})
	_, err = m.Write(true)
	var e *dexpp.Error
	if errors.As(err, &e) && e.Code == dexpp.CodeInvalidModel {
		fmt.Println(len(e.Problems) > 0, e.Problems[0].Where != "" || e.Problems[0].Message != "")
	}
	// --8<-- [end:errors]

	// Output:
	// true true
	// true
	// true true
}
