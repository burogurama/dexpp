# Call graph

The call graph says which method can call which. It covers every method of
every loaded DEX file. It is built the first time you ask the analysis context
for it (Python `ctx.call_graph`, Go `ctx.CallGraph()`, Java
`ctx.callGraph()`), and kept after that.

The examples come in Python, Go and Java. Pick your language in the tabs. Each
example uses a small `label` helper that writes a node as its class and its
method name; the first example defines it.

<!-- example: call_graph.graph -->

## Nodes and edges

A **node** is a method. Its id is its `MethodId`, and its kind says what the
node stands for:

- `DEFINED`: a method defined in a loaded file.
- `EXTERNAL`: a method the app calls but does not contain, such as a framework
  method.
- `CALL_SITE`: an `invoke-custom` call site (see
  [What the graph does not model](#what-the-graph-does-not-model)). Its id is
  opaque: it only keeps call sites apart and names no method.

In Python and Java the kinds are `CallNodeKind.DEFINED` and so on. In Go they
are `dexpp.CallNodeDefined`, `dexpp.CallNodeExternal` and
`dexpp.CallNodeCallSite`, and they print in lower case.

An **edge** is one call site: one invoke instruction in the caller, at the
edge's code offset, pointing at a callee.

<!-- example: call_graph.node -->

The bindings reach nodes and edges in two ways:

- In **Python**, nodes and edges are objects that know their neighbours.
  `node.out_edges` are the calls the method makes, `node.in_edges` the calls
  into it, and `edge.caller` and `edge.callee` are nodes.
- In **Go and Java**, nodes and edges are numbered. A node or an edge is a
  plain value that holds its own number (`Index`, Java `index()`) and the
  numbers of its neighbours. The methods of the graph take numbers and return
  numbers, and `Node(i)` and `Edge(i)` (Java: `node(i)`, `edge(i)`) give the
  node or the edge with a number.

| What | Python | Go | Java |
| ---- | ------ | -- | ---- |
| the node of a method, or nothing | `graph.find(id)` | `graph.Find(id)` | `graph.find(id)` |
| the calls a method makes | `node.out_edges` | `graph.OutEdges(node.Index)` | `graph.outEdges(node.index())` |
| the calls into a method | `node.in_edges` | `graph.InEdges(node.Index)` | `graph.inEdges(node.index())` |
| the caller and the callee of an edge | `edge.caller`, `edge.callee` | `edge.CallerNode`, `edge.CalleeNode` (numbers) | `edge.callerNode()`, `edge.calleeNode()` (numbers) |
| the `Method` behind a `DEFINED` node | `node.method` | `graph.MethodFor(node.Index)` | `graph.methodFor(node.index())` |

- `find` gives nothing when the graph has no node for the method: `None` in
  Python, `false` as the second result in Go, an empty `Optional` in Java.
- The kind of an edge is the kind of invoke: `VIRTUAL`, `SUPER`, `DIRECT`,
  `STATIC`, `INTERFACE`, `POLYMORPHIC` or `CUSTOM` (Go: `dexpp.InvokeVirtual`
  and so on, printed in lower case).
- The method behind a node is absent for the other kinds of node: `None` in
  Python, `nil` in Go, an empty `Optional` in Java.

## Virtual dispatch

The bytecode of `Kennel.noise` calls `Animal.sound`. `Animal` is an interface,
so at runtime the call lands in whatever class the object really has: `Dog` or
`Cat`. The edge above only shows the *declared* target, `Animal.sound`.

The graph also knows the possible runtime targets. It finds them with
class-hierarchy analysis: it looks at every loaded class below the declared
one and collects the methods a call could land in.

<!-- example: call_graph.dispatch -->

| What | Python | Go | Java |
| ---- | ------ | -- | ---- |
| the methods a call declared against a node may run | `node.dispatch_targets` | `graph.DispatchTargets(i)` | `graph.dispatchTargets(i)` |
| the declared methods through which a call can reach a node | `node.dispatch_sources` | `graph.DispatchSources(i)` | `graph.dispatchSources(i)` |
| what a method can call, dispatch included | `node.callees()` | `graph.Callees(i)` | `graph.callees(i)` |
| who can call a method, dispatch included | `node.callers()` | `graph.Callers(i)` | `graph.callers(i)` |

- The dispatch targets are the other methods that a virtual or interface call
  declared against the node may run.
- The dispatch sources are the reverse: the declared methods through which a
  call can reach the node.
- The callees are the simple answer to "what can this method call": the callee
  of each outgoing edge plus the dispatch targets of each virtual or interface
  call.
- The callers are the simple answer to "who can call this method", with
  dispatch included.

The graph stores the dispatch targets and sources, so reading them is cheap.
The callees and the callers are collected again on each call: the library
goes through the edges, adds the dispatch targets, removes the duplicates and
sorts the result. For a method that is called from many places, the callers
go through every one of those call sites. Keep the result when you need it
more than once. (In Python this is why `dispatch_targets` is a property, but
`callees()` and `callers()` are methods.)

<!-- example: call_graph.callers -->

`Dog.sound` has no direct call from `Kennel.noise` or `Pet.describe` in the
bytecode. They appear because their calls to `Animal.sound` can dispatch to
it.

!!! note "Why dispatch targets are not edges"
    The dispatch targets depend only on the declared method, not on the call
    site. So they are stored once per method and shared by all its call sites.
    Stored as edges, a popular method such as `Object.toString()` would repeat
    its whole list of overrides at every call. On a large app that is hundreds
    of millions of edges.

Class-hierarchy analysis is an over-approximation. It lists every method the
call *could* reach given the classes in the app, not the ones it reaches in a
real run.

## Calls to a method the class inherits

Sometimes the bytecode names a class that does not define the method itself.
`Pet.describe` calls `sound()` on itself, and the bytecode says `Pet.sound`.
But `Pet` has no `sound` method: it inherits the declaration from `Animal`.

For such a call site the graph adds a second edge, to the nearest definition a
loaded ancestor provides. This is the method Android would find at runtime.

<!-- example: call_graph.inherited -->

Each edge has an origin (Python and Java `EdgeOrigin.DECLARED` and so on, Go
`dexpp.EdgeDeclared` and `dexpp.EdgeInheritedResolution`):

- `DECLARED`: the target exactly as the bytecode names it. Every call site has
  exactly one.
- `INHERITED_RESOLUTION`: the definition found by walking up the superclass
  chain, then the interfaces. A call site has at most one. It always points at
  a `DEFINED` node.

When no loaded ancestor defines the method, there is no second edge and the
declared node stays `EXTERNAL`. This is the normal case for a call that ends
in a framework class.

## External methods

<!-- example: call_graph.external -->

`LPet;->sound` is in this list for the reason above: no method with exactly
that identity exists. The others are Java library methods.

## Filtering edges

A large app has millions of call sites. Going through all of them in your own
code is slow, in Python most of all. The graph can filter them inside the
library:

<!-- example: call_graph.filter -->

| Language | Filter | Result |
| -------- | ------ | ------ |
| Python | `edges.where(origin=..., kind=...)`; leave out a filter to match any value | a view of the matching edges |
| Go | `graph.EdgesWhere(origin, kind)`; pass `dexpp.AnyEdgeOrigin` or `dexpp.AnyInvokeKind` to match any value | the numbers of the matching edges |
| Java | `graph.edgesWhere(origin, kind)`, `edgesWhere(origin)` or `edgesWhere(kind)`; `null` matches any value | the numbers of the matching edges |

Filtering on the origin `DECLARED` gives exactly one edge per call site. In
Python, `where` works on `graph.edges` and on a node's `out_edges` and
`in_edges`.

## Views and copies

- In **Python**, `graph.nodes`, `graph.edges`, and the node lists such as
  `dispatch_targets` and the result of `callers()` are read-only sequences
  that do not copy the graph. Indexing, slicing, `len()` and iteration work as
  on a list. Nodes and edges compare equal when they are the same node or edge
  of the same graph, and they can be put in sets and used as dictionary keys.
- In **Go and Java**, `Nodes()` and `Edges()` (Java: `nodes()`, `edges()`)
  copy the whole table, so call them once, not inside a loop. `Node(i)` and
  `Edge(i)` copy one entry. `NodeCount()` and `EdgeCount()` (Java:
  `nodeCount()`, `edgeCount()`) give the sizes without copying. The lists of
  numbers that the other methods return are copies too.

## What the graph does not model

- **Reflection.** A method called through `Method.invoke` has no edge.
- **`invoke-custom`** (lambdas and string concatenation on newer compilers).
  Each call site becomes a node of its own, of kind `CALL_SITE`. The graph
  does not follow it to the method the call site is bound to, such as a
  lambda body.
- **`invoke-polymorphic`.** Only the declared `MethodHandle` target is
  recorded.
- **Framework callbacks.** Android calls methods such as `onCreate` itself.
  Nothing in the app calls them, so they have no callers in the graph.

When the same class is defined in more than one loaded DEX file, the first
definition wins, as on Android. "First" means the earliest DEX file in load
order: for an APK, `classes.dex`, then `classes2.dex`, and so on. An Android
class loader searches the DEX files in that order and stops at the first one
that defines the class, so a later definition is never loaded and its code
never runs. The graph does the same:

- only the first definition's methods are `DEFINED` nodes, and the method
  behind such a node is the method of that definition;
- only the code of that definition adds edges. A method that only a later
  definition has gets no node from it, and calls made in a later definition's
  code are not in the graph.

Looking up a class by descriptor, the class hierarchy and the cross-references
use the same definition. The list of every class still lists every
definition.
