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

A **node** is a method. `node.id` is its `MethodId`. A node is *resolved* when
the method is defined in a loaded file. Methods the app calls but does not
contain, such as framework methods, are nodes too, with `resolved == False`.

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
- `node.method` is the `Method` behind a resolved node, and `None` for an
  external one.

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
- `node.callees` is the simple answer to "what can this method call": the
  callee of each outgoing edge plus the dispatch targets of each virtual or
  interface call.
- `node.callers` is the simple answer to "who can call this method", with
  dispatch included.

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
  a resolved node.

When no loaded ancestor defines the method, there is no second edge and the
declared node stays unresolved. This is the normal case for a call that ends
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

`graph.nodes`, `graph.edges`, and the node lists such as `callers` are
read-only sequences that do not copy the graph. Indexing, slicing, `len()` and
iteration work as on a list. Nodes and edges compare equal when they are the
same node or edge of the same graph, and they can be put in sets and used as
dictionary keys.

## What the graph does not model

- **Reflection.** A method called through `Method.invoke` has no edge.
- **`invoke-custom`** (lambdas and string concatenation on newer compilers).
  Each call site becomes an unresolved node of its own. The graph does not
  follow it to the method the call site is bound to, such as a lambda body.
- **`invoke-polymorphic`.** Only the declared `MethodHandle` target is
  recorded.
- **Framework callbacks.** Android calls methods such as `onCreate` itself.
  Nothing in the app calls them, so they have no callers in the graph.

When the same method is defined in more than one loaded DEX file, the last
definition wins, as on Android.
