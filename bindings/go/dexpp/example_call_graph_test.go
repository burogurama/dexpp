package dexpp_test

// The testable examples of the guide "Call graph". docs_examples_test.go says
// how the guide uses them. Code outside the markers is not shown: it sets up
// the variables that the earlier sections of the guide made.

import (
	"fmt"
	"log"
	"sort"

	"github.com/burogurama/dexpp/bindings/go/dexpp"
)

// labeler returns the label function of the guide's first section.
func labeler(graph *dexpp.CallGraph) func(uint32) string {
	return func(index uint32) string {
		node, _ := graph.Node(index)
		return node.ID.ClassDescriptor + "->" + node.ID.Name
	}
}

// polymorphicGraph loads polymorphic.dex and builds its call graph.
func polymorphicGraph() (*dexpp.Context, *dexpp.CallGraph) {
	ctx, err := dexpp.FromDEX("../../../tests/data/polymorphic.dex")
	if err != nil {
		log.Fatal(err)
	}
	graph, err := ctx.CallGraph()
	if err != nil {
		log.Fatal(err)
	}
	return ctx, graph
}

// find returns the node of a method, and stops the example when it is missing.
func find(graph *dexpp.CallGraph, id dexpp.MethodID) dexpp.CallNode {
	node, ok := graph.Find(id)
	if !ok {
		log.Fatal("no node for ", id)
	}
	return node
}

// Build the call graph of a program.
func ExampleContext_CallGraph() {
	// --8<-- [start:graph]
	ctx, err := dexpp.FromDEX("../../../tests/data/polymorphic.dex")
	if err != nil {
		log.Fatal(err)
	}
	defer ctx.Close()
	graph, err := ctx.CallGraph()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(graph.NodeCount(), "methods,", graph.EdgeCount(), "edges")

	// label is a short name for a node: its class and its method name.
	label := func(index uint32) string {
		node, _ := graph.Node(index)
		return node.ID.ClassDescriptor + "->" + node.ID.Name
	}
	// --8<-- [end:graph]
	_ = label // the next sections of the guide use it

	// Output:
	// 16 methods, 18 edges
}

// Find the node of a method, and list the calls it makes.
func ExampleCallGraph_Find() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()
	label := labeler(graph)

	// --8<-- [start:node]
	noiseID := dexpp.MethodID{
		ClassDescriptor: "LKennel;", Name: "noise", Proto: "(LAnimal;)Ljava/lang/String;",
	}
	noise, ok := graph.Find(noiseID)
	if !ok {
		log.Fatal("no node for Kennel.noise")
	}
	fmt.Println(label(noise.Index), "kind:", noise.Kind)

	for _, index := range graph.OutEdges(noise.Index) {
		edge, _ := graph.Edge(index)
		fmt.Println("  calls", label(edge.CalleeNode), edge.Kind, "at offset", edge.CodeOffset)
	}
	// --8<-- [end:node]

	// Output:
	// LKennel;->noise kind: defined
	//   calls LAnimal;->sound interface at offset 0
}

// List the methods a virtual or interface call may land in.
func ExampleCallGraph_DispatchTargets() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()
	label := labeler(graph)
	noiseID := dexpp.MethodID{
		ClassDescriptor: "LKennel;", Name: "noise", Proto: "(LAnimal;)Ljava/lang/String;",
	}
	noise := find(graph, noiseID)

	// --8<-- [start:dispatch]
	soundID := dexpp.MethodID{
		ClassDescriptor: "LAnimal;", Name: "sound", Proto: "()Ljava/lang/String;",
	}
	sound, ok := graph.Find(soundID)
	if !ok {
		log.Fatal("no node for Animal.sound")
	}

	var targets, callees []string
	for _, index := range graph.DispatchTargets(sound.Index) {
		targets = append(targets, label(index))
	}
	for _, index := range graph.Callees(noise.Index) {
		callees = append(callees, label(index))
	}
	sort.Strings(targets)
	sort.Strings(callees)
	fmt.Println(targets)
	fmt.Println(callees)
	// --8<-- [end:dispatch]

	// Output:
	// [LCat;->sound LDog;->sound]
	// [LAnimal;->sound LCat;->sound LDog;->sound]
}

// List every method that may call a method, through dispatch too.
func ExampleCallGraph_Callers() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()
	label := labeler(graph)

	// --8<-- [start:callers]
	dogSoundID := dexpp.MethodID{
		ClassDescriptor: "LDog;", Name: "sound", Proto: "()Ljava/lang/String;",
	}
	dogSound, ok := graph.Find(dogSoundID)
	if !ok {
		log.Fatal("no node for Dog.sound")
	}

	var callers []string
	for _, index := range graph.Callers(dogSound.Index) {
		callers = append(callers, label(index))
	}
	sort.Strings(callers)
	fmt.Println(callers)
	// --8<-- [end:callers]

	// Output:
	// [LKennel;->dogNoise LKennel;->noise LPet;->describe]
}

// List the methods the program calls but does not contain.
func ExampleCallGraph_Nodes() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()
	label := labeler(graph)

	// --8<-- [start:external]
	var external []string
	var first dexpp.CallNode
	for _, node := range graph.Nodes() {
		if node.Kind == dexpp.CallNodeExternal {
			if len(external) == 0 {
				first = node
			}
			external = append(external, label(node.Index))
		}
	}
	sort.Strings(external)
	fmt.Println(external)

	noMethod := graph.MethodFor(first.Index)
	fmt.Println(noMethod)
	// --8<-- [end:external]

	// Output:
	// [LPet;->sound Ljava/lang/Object;-><init> Ljava/lang/StringBuilder;-><init> Ljava/lang/StringBuilder;->append Ljava/lang/StringBuilder;->toString]
	// <nil>
}

// Filter the edges inside the library.
func ExampleCallGraph_EdgesWhere() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()

	// --8<-- [start:filter]
	declared := graph.EdgesWhere(dexpp.EdgeDeclared, dexpp.AnyInvokeKind)
	interfaceCalls := graph.EdgesWhere(dexpp.AnyEdgeOrigin, dexpp.InvokeInterface)
	fmt.Println(len(declared), "call sites,", len(interfaceCalls), "of them invoke-interface")
	// --8<-- [end:filter]

	// Output:
	// 17 call sites, 1 of them invoke-interface
}

// Find the calls that name a class which only inherits the method.
func ExampleEdgeOrigin() {
	ctx, graph := polymorphicGraph()
	defer ctx.Close()
	label := labeler(graph)

	// --8<-- [start:inherited]
	for _, index := range graph.EdgesWhere(dexpp.EdgeInheritedResolution, dexpp.AnyInvokeKind) {
		edge, _ := graph.Edge(index)
		fmt.Println(label(edge.CallerNode), "resolves to", label(edge.CalleeNode))
	}
	// --8<-- [end:inherited]

	// Output:
	// LPet;->describe resolves to LAnimal;->sound
}
