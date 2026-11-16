package dexpp_test

// The testable examples of the guide "Classes, methods and bytecode".
// docs_examples_test.go says how the guide uses them. Code outside the markers
// is not shown: it sets up the variables that the earlier sections of the guide
// made.

import (
	"fmt"
	"log"
	"slices"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// loadClass loads a DEX file and finds one class in it.
func loadClass(path, descriptor string) (*dexpp.Context, *dexpp.Class) {
	ctx, err := dexpp.FromDEX(path)
	if err != nil {
		log.Fatal(err)
	}
	cls := ctx.FindClass(descriptor)
	if cls == nil {
		log.Fatal("no class ", descriptor)
	}
	return ctx, cls
}

// methodNamed finds a method of a class by its name.
func methodNamed(cls *dexpp.Class, name string) *dexpp.Method {
	for _, method := range cls.Methods() {
		if method.Name() == name {
			return method
		}
	}
	log.Fatal("no method ", name)
	return nil
}

// Load the code of an APK, of a DEX file, or of several DEX files together.
func ExampleFromAPK() {
	// --8<-- [start:load]
	apkCtx, err := dexpp.FromAPK("../../../tests/data/app.apk")
	if err != nil {
		log.Fatal(err)
	}
	defer apkCtx.Close()

	dexCtx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer dexCtx.Close()

	splitCtx, err := dexpp.FromDEXFiles([]string{
		"../../../tests/data/inherit_a.dex",
		"../../../tests/data/inherit_b.dex",
	})
	if err != nil {
		log.Fatal(err)
	}
	defer splitCtx.Close()

	fmt.Println(len(apkCtx.Classes()), len(dexCtx.Classes()), len(splitCtx.Classes()))
	// --8<-- [end:load]

	// Output:
	// 2 1 4
}

// List the classes of a DEX file, and look one up by its descriptor.
func ExampleContext_Classes() {
	// --8<-- [start:classes]
	ctx, err := dexpp.FromDEX("../../../tests/data/shapes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()

	for _, cls := range ctx.Classes() {
		kind := "class"
		if cls.IsInterface() {
			kind = "interface"
		}
		parent := "none"
		if superclass := cls.Superclass(); superclass != nil {
			parent = superclass.Descriptor()
		}
		interfaces := []string{}
		for _, ref := range cls.Interfaces() {
			interfaces = append(interfaces, ref.Descriptor())
		}
		fmt.Println(kind, cls.Descriptor(), "extends", parent, "implements", interfaces)
	}

	circle := ctx.FindClass("LCircle;")
	if circle == nil {
		log.Fatal("no Circle class")
	}
	var fields []string
	for _, field := range circle.Fields() {
		fields = append(fields, field.Name()+":"+field.Type())
	}
	fmt.Println(fields)

	missing := ctx.FindClass("Lno/such/Class;")
	fmt.Println(missing)
	// --8<-- [end:classes]

	// Output:
	// interface LShape; extends Ljava/lang/Object; implements []
	// class LAbstractShape; extends Ljava/lang/Object; implements [LShape;]
	// class LCircle; extends LAbstractShape; implements []
	// class LRectangle; extends LAbstractShape; implements []
	// [radius:D]
	// <nil>
}

// List the methods of a class, and test an access flag.
func ExampleClass_Methods() {
	ctx, circle := loadClass("../../../tests/data/shapes.dex", "LCircle;")
	defer ctx.Close()

	// --8<-- [start:methods]
	for _, method := range circle.Methods() {
		visibility := "non-public"
		if method.IsPublic() {
			visibility = "public"
		}
		fmt.Println(visibility, method.Name(), method.Parameters(), "->", method.ReturnType())
	}

	constructor := circle.Methods()[0]
	flags := constructor.AccessFlags()
	fmt.Println(constructor.Name(), uint32(flags), flags.Has(dexpp.AccessConstructor))
	// --8<-- [end:methods]

	// Output:
	// non-public <init> [Ljava/lang/String; D] -> V
	// public area [] -> D
	// public getRadius [] -> D
	// public perimeter [] -> D
	// <init> 65536 true
}

// Disassemble a method.
func ExampleMethod_Instructions() {
	ctx, circle := loadClass("../../../tests/data/shapes.dex", "LCircle;")
	defer ctx.Close()

	// --8<-- [start:instructions]
	var area *dexpp.Method
	for _, method := range circle.Methods() {
		if method.Name() == "area" {
			area = method
		}
	}
	instructions := area.Instructions()

	for _, instruction := range instructions {
		fmt.Printf("%04x  %s\n", instruction.Base().Offset, dexpp.ToString(instruction))
	}
	// --8<-- [end:instructions]

	// Output:
	// 0000  iget-wide v0, v4.field@1
	// 0002  const-wide v2, #4614256656552045848
	// 0007  mul-double v2, v2, v0
	// 0009  mul-double v2, v2, v0
	// 000b  return-wide v2
}

// Write a method's code as text, with every reference named.
func ExampleMethod_Disassemble() {
	// --8<-- [start:disassemble]
	testCtx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer testCtx.Close()
	testClass := testCtx.FindClass("LTestDex;")
	if testClass == nil {
		log.Fatal("no TestDex class")
	}

	for _, method := range testClass.Methods() {
		if method.Name() == "helloDex" || method.Name() == "main" {
			fmt.Println(method.Name() + method.Proto())
			for _, line := range method.Disassemble() {
				fmt.Printf("%04x  %s\n", line.Offset, line.Text)
			}
		}
	}
	// --8<-- [end:disassemble]

	// Output:
	// helloDex()Ljava/lang/String;
	// 0000  const-string v0, "Hello Dex++!!"
	// 0002  return-object v0
	// main([Ljava/lang/String;)V
	// 0000  sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;
	// 0002  invoke-static {}, LTestDex;->helloDex()Ljava/lang/String;
	// 0005  move-result-object v0
	// 0006  invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V
	// 0009  return-void
}

// Tell instruction kinds apart, and list what a method calls and accesses.
func ExampleMethod_Calls() {
	ctx, circle := loadClass("../../../tests/data/shapes.dex", "LCircle;")
	defer ctx.Close()
	area := methodNamed(circle, "area")
	instructions := area.Instructions()

	// --8<-- [start:typed]
	for _, instruction := range instructions {
		if field, ok := instruction.(dexpp.FieldInstruction); ok {
			action := "read"
			if field.IsWrite {
				action = "write"
			}
			fmt.Println("field access at", field.Offset, action)
		}
	}

	called := []string{}
	for _, call := range area.Calls() {
		called = append(called, call.Target.ClassDescriptor+" "+call.Target.Name)
	}
	accessed := []string{}
	for _, access := range area.FieldAccesses() {
		accessed = append(accessed, fmt.Sprintf("%s write=%v", access.Field.Name, access.IsWrite))
	}
	fmt.Println(called)
	fmt.Println(accessed)
	// --8<-- [end:typed]

	// Output:
	// field access at 0 read
	// []
	// [radius write=false]
}

// Read the register frame of a method.
func ExampleMethod_ParameterRegisters() {
	ctx, circle := loadClass("../../../tests/data/shapes.dex", "LCircle;")
	defer ctx.Close()
	area := methodNamed(circle, "area")

	// --8<-- [start:registers]
	fmt.Println(area.RegisterCount(), "registers")

	for _, parameter := range area.ParameterRegisters() {
		note := ""
		if parameter.IsThis {
			note = " (this)"
		}
		fmt.Printf("v%d %s%s\n", parameter.Reg, parameter.Type, note)
	}
	// --8<-- [end:registers]

	// Output:
	// 5 registers
	// v4 LCircle; (this)
}

// Walk the control-flow graph of a method.
func ExampleMethod_Cfg() {
	// --8<-- [start:cfg]
	fibonacciCtx, err := dexpp.FromDEX("../../../tests/data/fibonacci.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer fibonacciCtx.Close()
	fibonacci := fibonacciCtx.FindClass("LFibonacci;")
	if fibonacci == nil {
		log.Fatal("no Fibonacci class")
	}
	var compute *dexpp.Method
	for _, method := range fibonacci.Methods() {
		if method.Name() == "compute" {
			compute = method
		}
	}

	for _, block := range compute.Cfg().Blocks {
		edges := []string{}
		for _, edge := range block.Successors {
			edges = append(edges, fmt.Sprintf("%v->%d", edge.Kind, edge.TargetBlock))
		}
		fmt.Printf("block %d [%#x, %#x) %v\n", block.Index, block.StartOffset, block.EndOffset, edges)
	}
	// --8<-- [end:cfg]

	// Output:
	// block 0 [0x0, 0x2) [branch-taken->2 branch-not-taken->1]
	// block 1 [0x2, 0x5) []
	// block 2 [0x5, 0x9) [branch-taken->4 branch-not-taken->3]
	// block 3 [0x9, 0xa) []
	// block 4 [0xa, 0x14) [fallthrough->5]
	// block 5 [0x14, 0x16) [branch-taken->7 branch-not-taken->6]
	// block 6 [0x16, 0x20) [goto->5]
	// block 7 [0x20, 0x23) []
}

// Find the edges that lead to exception handlers.
func ExampleCfgEdge() {
	// --8<-- [start:handlers]
	parseCtx, err := dexpp.FromDEX("../../../tests/data/trycatch.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer parseCtx.Close()
	parseClass := parseCtx.FindClass("LTryCatch;")
	if parseClass == nil {
		log.Fatal("no TryCatch class")
	}
	var safeParse *dexpp.Method
	for _, method := range parseClass.Methods() {
		if method.Name() == "safeParse" {
			safeParse = method
		}
	}

	for _, block := range safeParse.Cfg().Blocks {
		for _, edge := range block.Successors {
			switch edge.Kind {
			case dexpp.CfgException:
				fmt.Printf("block %d -> %d: catches %s\n", block.Index, edge.TargetBlock, edge.CatchType)
			case dexpp.CfgExceptionCatchAll:
				fmt.Printf("block %d -> %d: catches everything\n", block.Index, edge.TargetBlock)
			}
		}
	}
	// --8<-- [end:handlers]

	// Output:
	// block 1 -> 4: catches Ljava/lang/NumberFormatException;
	// block 1 -> 3: catches everything
	// block 5 -> 3: catches everything
}

// List the try blocks of a method and their handlers.
func ExampleMethod_TryBlocks() {
	// --8<-- [start:try]
	tryCatchCtx, err := dexpp.FromDEX("../../../tests/data/trycatch.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer tryCatchCtx.Close()
	tryCatch := tryCatchCtx.FindClass("LTryCatch;")
	if tryCatch == nil {
		log.Fatal("no TryCatch class")
	}
	var divide *dexpp.Method
	for _, method := range tryCatch.Methods() {
		if method.Name() == "divide" {
			divide = method
		}
	}

	for _, tryBlock := range divide.TryBlocks() {
		fmt.Printf("try [%#x, %#x)\n", tryBlock.StartOffset, tryBlock.EndOffset)
		for _, handler := range tryBlock.Handlers {
			fmt.Printf("  catch %s at %#x\n", handler.TypeDescriptor, handler.HandlerOffset)
		}
		if tryBlock.HasCatchAll {
			fmt.Printf("  catch-all at %#x\n", tryBlock.CatchAllOffset)
		} else {
			fmt.Println("  no catch-all")
		}
	}
	// --8<-- [end:try]

	// Output:
	// try [0x0, 0x1)
	//   catch Ljava/lang/ArithmeticException; at 0x2
	//   no catch-all
}

// Read the annotations of a class and the initial values of its static fields.
func ExampleClass_Annotations() {
	// --8<-- [start:annotations]
	annotatedCtx, err := dexpp.FromDEX("../../../tests/data/annotated.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer annotatedCtx.Close()
	annotated := annotatedCtx.FindClass("LAnnotated;")
	if annotated == nil {
		log.Fatal("no Annotated class")
	}

	for _, annotation := range annotated.Annotations() {
		fmt.Println(annotation.TypeDescriptor, annotation.Visibility)
		for _, element := range annotation.Elements {
			fmt.Printf("  %s = %v (%T)\n", element.Name, element.Value, element.Value)
		}
	}

	for _, field := range annotated.Fields() {
		if field.Name() == "MAGIC" || field.Name() == "GREETING" {
			value, _ := field.InitialValue()
			fmt.Printf("%s = %v (%T)\n", field.Name(), value, value)
		}
	}
	// --8<-- [end:annotations]

	// Output:
	// LMarker; runtime
	//   level = 3 (int64)
	//   name = cls (string)
	//   tags = [a b] ([]interface {})
	// GREETING = hello (string)
	// MAGIC = 42 (int64)
}

// Read the whole string table.
func ExampleContext_Strings() {
	// --8<-- [start:strings]
	helloCtx, err := dexpp.FromDEX("../../../tests/data/classes.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer helloCtx.Close()

	strings := helloCtx.Strings()
	fmt.Println(len(strings), "strings;", slices.Contains(strings, "Hello Dex++!!"))
	// --8<-- [end:strings]

	// Output:
	// 17 strings; true
}
