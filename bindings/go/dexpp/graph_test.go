package dexpp

import (
	"reflect"
	"slices"
	"testing"
)

func mustCallGraph(t *testing.T, ctx *Context) *CallGraph {
	t.Helper()
	g, err := ctx.CallGraph()
	if err != nil {
		t.Fatalf("CallGraph: %v", err)
	}
	t.Cleanup(g.Close)
	return g
}

func mustNode(t *testing.T, g *CallGraph, id MethodID) CallNode {
	t.Helper()
	n, ok := g.Find(id)
	if !ok {
		t.Fatalf("Find(%v) found nothing", id)
	}
	return n
}

// classesOf returns the class descriptors of some nodes, sorted.
func classesOf(t *testing.T, g *CallGraph, nodes []uint32) []string {
	t.Helper()
	var out []string
	for _, i := range nodes {
		n, ok := g.Node(i)
		if !ok {
			t.Fatalf("Node(%d) found nothing", i)
		}
		out = append(out, n.ID.ClassDescriptor)
	}
	slices.Sort(out)
	return out
}

func namesOf(t *testing.T, g *CallGraph, nodes []uint32) []string {
	t.Helper()
	var out []string
	for _, i := range nodes {
		n, _ := g.Node(i)
		out = append(out, n.ID.Name)
	}
	slices.Sort(out)
	return out
}

func TestCallGraph(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	g := mustCallGraph(t, ctx)
	if g.Empty() || g.NodeCount() == 0 || g.EdgeCount() == 0 {
		t.Fatal("empty call graph")
	}

	sound := mustNode(t, g, MethodID{"LAnimal;", "sound", "()Ljava/lang/String;"})
	if !sound.Resolved {
		t.Error("Animal.sound is not resolved")
	}
	if _, ok := g.Find(MethodID{"LNo;", "where", "()V"}); ok {
		t.Error("Find of an unknown method found a node")
	}

	// The interface call may dispatch to the Dog and Cat overrides. The
	// targets are stored once on the declared node.
	if got := classesOf(t, g, g.DispatchTargets(sound.Index)); !slices.Equal(got, []string{"LCat;", "LDog;"}) {
		t.Errorf("dispatch targets of Animal.sound = %v", got)
	}
	noise := mustNode(t, g, MethodID{"LKennel;", "noise", "(LAnimal;)Ljava/lang/String;"})
	var site CallEdge
	found := false
	for _, i := range g.OutEdges(noise.Index) {
		e, _ := g.Edge(i)
		if e.CalleeNode == sound.Index {
			site, found = e, true
		}
	}
	if !found || site.Origin != EdgeDeclared || site.Kind != InvokeInterface {
		t.Fatalf("Kennel.noise call site = %+v, found %v", site, found)
	}
	if got := g.EdgeDispatchTargets(site.Index); !slices.Equal(got, g.DispatchTargets(sound.Index)) {
		t.Errorf("site dispatch targets = %v", got)
	}

	// Pet defines no sound(), so the call site in Pet.describe resolves upward
	// to the abstract Animal.sound.
	nodes, edges := g.Nodes(), g.Edges()
	var inherited []CallEdge
	for _, e := range edges {
		if e.Origin == EdgeInheritedResolution && nodes[e.CallerNode].ID.ClassDescriptor == "LPet;" {
			inherited = append(inherited, e)
		}
	}
	if len(inherited) != 1 {
		t.Fatalf("inherited edges from LPet; = %+v", inherited)
	}
	callee := nodes[inherited[0].CalleeNode]
	if callee.ID.ClassDescriptor != "LAnimal;" || !callee.Resolved {
		t.Errorf("inherited callee = %+v", callee)
	}
	// A site's dispatch targets hang off its declared edge only.
	if got := g.EdgeDispatchTargets(inherited[0].Index); got != nil {
		t.Errorf("an inherited edge has dispatch targets: %v", got)
	}

	// Every call site has exactly one declared edge.
	type siteKey struct{ caller, offset uint32 }
	sites := map[siteKey]int{}
	for _, e := range edges {
		if e.Origin == EdgeDeclared {
			sites[siteKey{e.CallerNode, e.CodeOffset}]++
		}
	}
	for k, n := range sites {
		if n != 1 {
			t.Errorf("site %+v has %d declared edges", k, n)
		}
	}

	// The reverse direction and the neighbour lists include dispatch.
	dog := mustNode(t, g, MethodID{"LDog;", "sound", "()Ljava/lang/String;"})
	if got := classesOf(t, g, g.DispatchSources(dog.Index)); !slices.Equal(got, []string{"LAnimal;", "LPet;"}) {
		t.Errorf("dispatch sources of Dog.sound = %v", got)
	}
	if got := namesOf(t, g, g.Callers(dog.Index)); !slices.Equal(got, []string{"describe", "dogNoise", "noise"}) {
		t.Errorf("callers of Dog.sound = %v", got)
	}
	if got := classesOf(t, g, g.Callees(noise.Index)); !slices.Equal(got, []string{"LAnimal;", "LCat;", "LDog;"}) {
		t.Errorf("callees of Kennel.noise = %v", got)
	}

	m := g.MethodFor(sound.Index)
	if m == nil || m.Name() != "sound" || m.HasCode() {
		t.Errorf("MethodFor(Animal.sound) = %v", m)
	}
}

func TestCallGraphTablesAndLookups(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	g := mustCallGraph(t, ctx)
	nodes, edges := g.Nodes(), g.Edges()
	if len(nodes) != g.NodeCount() || len(edges) != g.EdgeCount() {
		t.Fatalf("%d nodes, %d edges; counts say %d and %d", len(nodes), len(edges),
			g.NodeCount(), g.EdgeCount())
	}
	for i, n := range nodes {
		if n.Index != uint32(i) {
			t.Errorf("node %d has index %d", i, n.Index)
		}
		if one, ok := g.Node(uint32(i)); !ok || !reflect.DeepEqual(one, n) {
			t.Errorf("Node(%d) = %+v, want %+v", i, one, n)
		}
		if found, ok := g.Find(n.ID); !ok || found.Index != n.Index {
			t.Errorf("Find(%v) = %+v", n.ID, found)
		}
		if m := g.MethodFor(n.Index); (m != nil) != n.Resolved {
			t.Errorf("MethodFor(%v) = %v, resolved %v", n.ID, m, n.Resolved)
		} else if m != nil && m.ID() != n.ID {
			t.Errorf("MethodFor(%v) gives %v", n.ID, m.ID())
		}
	}
	for i, e := range edges {
		if e.Index != uint32(i) {
			t.Errorf("edge %d has index %d", i, e.Index)
		}
		if one, ok := g.Edge(uint32(i)); !ok || one != e {
			t.Errorf("Edge(%d) = %+v, want %+v", i, one, e)
		}
		if !slices.Contains(g.OutEdges(e.CallerNode), e.Index) ||
			!slices.Contains(g.InEdges(e.CalleeNode), e.Index) {
			t.Errorf("edge %d is missing from its nodes' edge lists", i)
		}
	}

	// Out-of-range indices find nothing.
	far := uint32(len(nodes) + 10)
	if _, ok := g.Node(far); ok {
		t.Error("Node out of range found a node")
	}
	if _, ok := g.Edge(uint32(len(edges) + 10)); ok {
		t.Error("Edge out of range found an edge")
	}
	if g.MethodFor(far) != nil || g.OutEdges(far) != nil || g.InEdges(far) != nil ||
		g.Callers(far) != nil || g.Callees(far) != nil || g.DispatchTargets(far) != nil ||
		g.DispatchSources(far) != nil || g.EdgeDispatchTargets(uint32(len(edges)+10)) != nil {
		t.Error("a query out of range returned something")
	}

	// The neighbour lists are sorted and have no duplicates.
	for _, n := range nodes {
		for _, list := range [][]uint32{g.DispatchTargets(n.Index), g.DispatchSources(n.Index),
			g.Callers(n.Index), g.Callees(n.Index)} {
			if !slices.IsSorted(list) || len(slices.Compact(slices.Clone(list))) != len(list) {
				t.Errorf("list %v of %v is not sorted or has duplicates", list, n.ID)
			}
		}
	}
	external := false
	for _, n := range nodes {
		external = external || !n.Resolved
	}
	if !external {
		t.Error("no external node")
	}
}

func TestEdgesWhereFiltersInTheLibrary(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	g := mustCallGraph(t, ctx)
	edges := g.Edges()
	filter := func(origin EdgeOrigin, kind InvokeKind) []uint32 {
		var out []uint32
		for _, e := range edges {
			if (origin == AnyEdgeOrigin || e.Origin == origin) && (kind == AnyInvokeKind || e.Kind == kind) {
				out = append(out, e.Index)
			}
		}
		return out
	}
	for _, c := range []struct {
		origin EdgeOrigin
		kind   InvokeKind
	}{
		{EdgeDeclared, AnyInvokeKind}, {EdgeInheritedResolution, InvokeVirtual},
		{AnyEdgeOrigin, InvokeDirect}, {AnyEdgeOrigin, AnyInvokeKind},
	} {
		got := g.EdgesWhere(c.origin, c.kind)
		if want := filter(c.origin, c.kind); !slices.Equal(got, want) {
			t.Errorf("EdgesWhere(%v, %v) = %v, want %v", c.origin, c.kind, got, want)
		}
	}
	if len(g.EdgesWhere(EdgeInheritedResolution, InvokeVirtual)) == 0 {
		t.Error("no inherited virtual edge")
	}
	if got := len(g.EdgesWhere(AnyEdgeOrigin, AnyInvokeKind)); got != len(edges) {
		t.Errorf("EdgesWhere(any, any) has %d edges, want %d", got, len(edges))
	}
}

func TestCallGraphDoesNotDependOnTheThreadCount(t *testing.T) {
	type graph struct {
		Nodes    []CallNode
		Edges    []CallEdge
		Dispatch [][]uint32
	}
	build := func(opts ...LoadOption) graph {
		ctx, err := FromAPK(data("multidex.apk"), opts...)
		if err != nil {
			t.Fatal(err)
		}
		defer ctx.Close()
		g := mustCallGraph(t, ctx)
		out := graph{Nodes: g.Nodes(), Edges: g.Edges()}
		for _, n := range out.Nodes {
			out.Dispatch = append(out.Dispatch, g.DispatchTargets(n.Index))
		}
		return out
	}
	one, automatic := build(WithThreads(1)), build()
	if len(one.Edges) == 0 {
		t.Fatal("no edges")
	}
	if !reflect.DeepEqual(one, automatic) {
		t.Error("the graph with one thread differs from the graph with automatic threads")
	}
}

func TestClassHierarchy(t *testing.T) {
	ctx := mustDEX(t, "polymorphic.dex")
	h, err := ctx.ClassHierarchy()
	if err != nil {
		t.Fatal(err)
	}
	defer h.Close()
	if h.Empty() {
		t.Fatal("empty hierarchy")
	}
	if !h.IsSubtypeOf("LDog;", "LAnimal;") || h.IsSubtypeOf("LAnimal;", "LDog;") ||
		!h.IsSubtypeOf("LDog;", "LDog;") {
		t.Error("IsSubtypeOf is wrong")
	}
	sorted := func(s []string) []string { slices.Sort(s); return s }
	if got := sorted(h.AllDescendants("LAnimal;")); !slices.Equal(got, []string{"LCat;", "LDog;", "LPet;"}) {
		t.Errorf("AllDescendants(LAnimal;) = %v", got)
	}
	if !h.IsInterface("LAnimal;") || h.IsInterface("LDog;") {
		t.Error("IsInterface is wrong")
	}
	if h.IsLoaded("Ljava/lang/Object;") || !h.IsLoaded("LDog;") {
		t.Error("IsLoaded is wrong")
	}
	if got := h.Supertypes("LPet;"); !slices.Equal(got, []string{"Ljava/lang/Object;", "LAnimal;"}) {
		t.Errorf("Supertypes(LPet;) = %v", got)
	}
	if got, ok := h.SuperclassOf("LDog;"); !ok || got != "LPet;" {
		t.Errorf("SuperclassOf(LDog;) = %q, %v", got, ok)
	}
	for _, d := range []string{"Ljava/lang/Object;", "LNo;"} {
		if got, ok := h.SuperclassOf(d); ok {
			t.Errorf("SuperclassOf(%s) = %q", d, got)
		}
	}
	if got := h.DirectInterfaces("LPet;"); !slices.Equal(got, []string{"LAnimal;"}) {
		t.Errorf("DirectInterfaces(LPet;) = %v", got)
	}
	if got := sorted(h.Subclasses("LPet;")); !slices.Equal(got, []string{"LCat;", "LDog;"}) {
		t.Errorf("Subclasses(LPet;) = %v", got)
	}
	if got := h.Implementers("LAnimal;"); !slices.Equal(got, []string{"LPet;"}) {
		t.Errorf("Implementers(LAnimal;) = %v", got)
	}
	if got := sorted(h.AllSupertypes("LDog;")); !slices.Equal(got, []string{"LAnimal;", "LPet;", "Ljava/lang/Object;"}) {
		t.Errorf("AllSupertypes(LDog;) = %v", got)
	}
	if h.Subclasses("LNo;") != nil || h.AllDescendants("LNo;") != nil || h.IsSubtypeOf("LNo;", "LDog;") {
		t.Error("a query about an unknown class returned something")
	}
}

func TestXrefs(t *testing.T) {
	ctx := mustDEX(t, "classes.dex")
	x, err := ctx.Xrefs()
	if err != nil {
		t.Fatal(err)
	}
	defer x.Close()
	if x.Empty() || x.ReferrerCount() == 0 {
		t.Fatal("empty index")
	}

	sites := x.StringRefs("Hello Dex++!!")
	if len(sites) != 1 {
		t.Fatalf("StringRefs = %+v", sites)
	}
	id, ok := x.ReferrerID(sites[0].Referrer)
	if !ok || id.Name != "helloDex" {
		t.Errorf("ReferrerID = %v, %v", id, ok)
	}
	if m := x.ReferrerMethod(sites[0].Referrer); m == nil || m.Name() != "helloDex" {
		t.Errorf("ReferrerMethod = %v", m)
	}
	referrers := x.Referrers()
	if len(referrers) != x.ReferrerCount() || referrers[sites[0].Referrer] != id {
		t.Errorf("Referrers = %v", referrers)
	}
	if _, ok := x.ReferrerID(uint32(len(referrers))); ok {
		t.Error("ReferrerID out of range found a method")
	}
	if x.ReferrerMethod(uint32(len(referrers))) != nil {
		t.Error("ReferrerMethod out of range found a method")
	}
	if !slices.Contains(x.ReferencedStrings(), "Hello Dex++!!") {
		t.Error("ReferencedStrings has no Hello Dex++!!")
	}
	if got := x.StringRefs("not in the program"); got != nil {
		t.Errorf("StringRefs of an unknown string = %+v", got)
	}

	out := FieldID{"Ljava/lang/System;", "out", "Ljava/io/PrintStream;"}
	refs := x.FieldRefs(out)
	if len(refs) != 1 || !refs[0].IsStatic || refs[0].IsWrite {
		t.Errorf("FieldRefs = %+v", refs)
	}
	if got := x.FieldReads(out); !slices.Equal(got, refs) {
		t.Errorf("FieldReads = %+v", got)
	}
	if got := x.FieldWrites(out); got != nil {
		t.Errorf("FieldWrites = %+v", got)
	}

	printlnID := MethodID{"Ljava/io/PrintStream;", "println", "(Ljava/lang/String;)V"}
	calls := x.MethodRefs(printlnID)
	if len(calls) != 1 || calls[0].Kind != InvokeVirtual {
		t.Errorf("MethodRefs = %+v", calls)
	}
	groups := x.MethodRefsByName("println")
	if len(groups) != 1 || groups[0].Target != printlnID || !slices.Equal(groups[0].Sites, calls) {
		t.Errorf("MethodRefsByName = %+v", groups)
	}
	if got := x.MethodRefsByName("noSuchName"); got != nil {
		t.Errorf("MethodRefsByName of an unknown name = %+v", got)
	}

	// Type references, and a string with a NUL byte.
	pets := mustDEX(t, "polymorphic.dex")
	px, err := pets.Xrefs()
	if err != nil {
		t.Fatal(err)
	}
	types := px.TypeRefs("Ljava/lang/StringBuilder;")
	if len(types) == 0 || types[0].Kind != TypeUseNewInstance {
		t.Errorf("TypeRefs = %+v", types)
	}
	multi := mustDEX(t, "multistring.dex")
	mx, err := multi.Xrefs()
	if err != nil {
		t.Fatal(err)
	}
	if got := mx.StringRefs("test\x00embedded"); len(got) != 1 {
		t.Errorf(`StringRefs("test\x00embedded") = %+v`, got)
	}
	if got := mx.StringRefs("test"); got != nil {
		t.Errorf(`StringRefs("test") = %+v, want none: the NUL byte is part of the string`, got)
	}
}
