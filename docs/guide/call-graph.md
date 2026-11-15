# Call graph

The call graph says which method can call which. It covers every method of
every loaded DEX file. It is built the first time you read `ctx.call_graph`,
and kept after that.

```python
--8<-- "call_graph.py:graph"
```

```text
--8<-- "output/call_graph.graph.txt"
```

## Nodes and edges

A **node** is a method. `node.id` is its `MethodId`, and `node.kind` says what
the node stands for:

- `CallNodeKind.DEFINED`: a method defined in a loaded file.
- `CallNodeKind.EXTERNAL`: a method the app calls but does not contain, such
  as a framework method.
- `CallNodeKind.CALL_SITE`: an `invoke-custom` call site (see
  [What the graph does not model](#what-the-graph-does-not-model)). Its `id`
  is opaque: it only keeps call sites apart and names no method.

An **edge** is one call site: one invoke instruction in the caller, at
`edge.code_offset`, pointing at a callee.

```python
--8<-- "call_graph.py:node"
```

```text
--8<-- "output/call_graph.node.txt"
```

- `graph.find(method_id)` returns the node of a method, or `None`.
- `node.out_edges` are the calls the method makes. `node.in_edges` are the
  calls into it.
- `edge.caller` and `edge.callee` are nodes. `edge.kind` is the kind of
  invoke: `VIRTUAL`, `SUPER`, `DIRECT`, `STATIC`, `INTERFACE`, `POLYMORPHIC`
  or `CUSTOM`.
- `node.method` is the `Method` behind a `DEFINED` node, and `None` for the
  other kinds.

## Virtual dispatch

The bytecode of `Kennel.noise` calls `Animal.sound`. `Animal` is an interface,
so at runtime the call lands in whatever class the object really has: `Dog` or
`Cat`. The edge above only shows the *declared* target, `Animal.sound`.

The graph also knows the possible runtime targets. It finds them with
class-hierarchy analysis: it looks at every loaded class below the declared
one and collects the methods a call could land in.

```python
--8<-- "call_graph.py:dispatch"
```

```text
--8<-- "output/call_graph.dispatch.txt"
```

- `node.dispatch_targets` are the other methods that a virtual or interface
  call declared against `node` may run.
- `node.dispatch_sources` is the reverse: the declared methods through which a
  call can reach `node`.
- `node.callees()` is the simple answer to "what can this method call": the
  callee of each outgoing edge plus the dispatch targets of each virtual or
  interface call.
- `node.callers()` is the simple answer to "who can call this method", with
  dispatch included.

`dispatch_targets` and `dispatch_sources` are properties, because the graph
stores them. `callees()` and `callers()` are methods, because each call
collects the nodes again, removes the duplicates and sorts them. For a method
that is called from many places, `callers()` goes through every one of those
call sites. Keep the result when you need it more than once.

```python
--8<-- "call_graph.py:callers"
```

```text
--8<-- "output/call_graph.callers.txt"
```

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

```python
--8<-- "call_graph.py:inherited"
```

```text
--8<-- "output/call_graph.inherited.txt"
```

Each edge has an `origin`:

- `DECLARED`: the target exactly as the bytecode names it. Every call site has
  exactly one.
- `INHERITED_RESOLUTION`: the definition found by walking up the superclass
  chain, then the interfaces. A call site has at most one. It always points at
  a `DEFINED` node.

When no loaded ancestor defines the method, there is no second edge and the
declared node stays `EXTERNAL`. This is the normal case for a call that ends
in a framework class.

## External methods

```python
--8<-- "call_graph.py:external"
```

```text
--8<-- "output/call_graph.external.txt"
```

`LPet;->sound` is in this list for the reason above: no method with exactly
that identity exists. The others are Java library methods.

## Filtering edges

A large app has millions of call sites. A Python loop over all of them is
slow. `where` filters inside the library and returns a view:

```python
--8<-- "call_graph.py:filter"
```

```text
--8<-- "output/call_graph.filter.txt"
```

`where(origin=EdgeOrigin.DECLARED)` gives exactly one edge per call site. It
works on `graph.edges` and on a node's `out_edges` and `in_edges`.

## Views

`graph.nodes`, `graph.edges`, and the node lists such as `dispatch_targets`
and the result of `callers()` are read-only sequences that do not copy the
graph. Indexing, slicing, `len()` and iteration work as on a list. Nodes and
edges compare equal when they are the same node or edge of the same graph, and
they can be put in sets and used as dictionary keys.

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

- only the first definition's methods are resolved nodes, and `node.method`
  returns the method of that definition;
- only the code of that definition adds edges. A method that only a later
  definition has gets no node from it, and calls made in a later definition's
  code are not in the graph.

`find_class`, the class hierarchy and the cross-references use the same
definition. `classes()` still lists every definition.
