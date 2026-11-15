# Class hierarchy and cross-references

Both structures cover the whole program. Each is built the first time you ask
the analysis context for it, and kept after that.

The examples come in Python, Go and Java. Pick your language in the tabs.

## Class hierarchy

The class hierarchy (Python `ctx.class_hierarchy`, Go `ctx.ClassHierarchy()`,
Java `ctx.classHierarchy()`) answers questions about inheritance. All queries
take and return descriptors.

<!-- example: hierarchy_xrefs.hierarchy -->

| Python | Go | Java | Returns |
| ------ | -- | ---- | ------- |
| `supertypes(d)` | `Supertypes(d)` | `supertypes(d)` | the direct superclass, then the directly implemented interfaces |
| `all_supertypes(d)` | `AllSupertypes(d)` | `allSupertypes(d)` | every ancestor class and interface; `d` itself is not included |
| `subclasses(d)` | `Subclasses(d)` | `subclasses(d)` | the classes that directly extend `d` |
| `implementers(d)` | `Implementers(d)` | `implementers(d)` | the classes that directly implement the interface `d` |
| `all_descendants(d)` | `AllDescendants(d)` | `allDescendants(d)` | everything below `d`, through `extends` and `implements`; `d` itself is not included |
| `is_subtype_of(sub, super)` | `IsSubtypeOf(sub, super)` | `isSubtypeOf(sub, super)` | whether `sub` is `super` or is below it |
| `is_loaded(d)` | `IsLoaded(d)` | `isLoaded(d)` | whether the class is defined in a loaded file |
| `is_interface(d)` | `IsInterface(d)` | `isInterface(d)` | whether the class is loaded and is an interface |

Classes outside the loaded files are still in the hierarchy when a loaded
class names them as a superclass or an interface. `Ljava/lang/Object;` is the
usual example. For such a class `is_loaded` is false, and its own ancestors
are unknown: the chain stops there.

A query about a descriptor the hierarchy has never seen returns an empty list,
or false.

When a class is defined in more than one loaded file, its superclass and
interfaces come from the first definition only, the one a lookup by descriptor
returns. Android loads that definition and never the later ones.

## Cross-references

The cross-reference index (Python `ctx.xrefs`, Go `ctx.Xrefs()`, Java
`ctx.xrefs()`) answers "who uses this". It indexes four kinds of use:

| Kind | Python | Go | Java | Indexed instructions |
| ---- | ------ | -- | ---- | -------------------- |
| Method | `method_refs(id)`, `method_refs_by_name(name)` | `MethodRefs(id)`, `MethodRefsByName(name)` | `methodRefs(id)`, `methodRefsByName(name)` | invokes |
| Field | `field_refs(id)`, `field_reads(id)`, `field_writes(id)` | `FieldRefs(id)`, `FieldReads(id)`, `FieldWrites(id)` | `fieldRefs(id)`, `fieldReads(id)`, `fieldWrites(id)` | `iget`, `iput`, `sget`, `sput` |
| String | `string_refs(value)` | `StringRefs(value)` | `stringRefs(value)` | `const-string` |
| Type | `type_refs(descriptor)` | `TypeRefs(descriptor)` | `typeRefs(descriptor)` | `const-class`, `check-cast`, `instance-of`, `new-instance`, `new-array`, `filled-new-array` |

Every result is a list of *sites*. A site has a code offset and a *referrer*:
the method whose code contains the instruction.

### Who calls a method

<!-- example: hierarchy_xrefs.method_refs -->

`method_refs_by_name` finds calls by the bare method name, in any class and
with any prototype. It is the quick way to find every call to `exec`,
`loadUrl` or `getInstance`. Each result pairs the exact `MethodId` that was
called with its sites. The results come in no fixed order.

`method_refs(id)` takes an exact `MethodId` instead.

Method references are literal: a site is listed under the target that the
bytecode names. A call to `Animal.sound` is not listed under `Dog.sound`. Use
the [call graph](call-graph.md) when you need virtual dispatch.

### The referrer

A site's referrer is a number. There are three ways to use it:

| To get | Python | Go | Java |
| ------ | ------ | -- | ---- |
| the referrer's `MethodId` | `xrefs.referrer_id(r)` or `xrefs.referrers[r]` | `xrefs.ReferrerID(r)` | `xrefs.referrerId(r)` |
| the `Method` itself | `xrefs.referrer_method(r)` | `xrefs.ReferrerMethod(r)` | `xrefs.referrerMethod(r)` |
| every referrer's `MethodId`, by number | `xrefs.referrers` | `xrefs.Referrers()` | `xrefs.referrers()` |

In Python, `xrefs.referrers` is a view that copies nothing, so
`xrefs.referrers[site.referrer]` is cheap inside a loop. In Go and Java,
`Referrers()` copies the whole table, so call it once, or use `ReferrerID`
(Java: `referrerId`) for one number. Use the `Method` when you need more than
the identity.

### Who loads a string

<!-- example: hierarchy_xrefs.strings -->

`referenced_strings()` (Go: `ReferencedStrings()`) lists every distinct string
that the code loads, in no fixed order. `string_refs` matches the whole string
exactly. To search by substring or pattern, go through `referenced_strings()`
first, then look up the ones that match:

<!-- example: hierarchy_xrefs.pattern -->

### Who reads or writes a field

<!-- example: hierarchy_xrefs.fields -->

`field_refs` returns reads and writes together. Each site says whether it is a
write and whether the field is static (Python `is_write` and `is_static`, Go
`IsWrite` and `IsStatic`, Java `isWrite()` and `isStatic()`).

### Who uses a type

<!-- example: hierarchy_xrefs.types -->

The kind of a site says what the instruction does with the type, such as
`NEW_INSTANCE` (Go: `dexpp.TypeUseNewInstance`, printed as `new-instance`).
For `NEW_ARRAY` the descriptor is the array type, such as `[I`.

### What is not indexed

- `invoke-custom` call sites.
- The operands of `const-method-handle` and `const-method-type`.
- Types that appear only in a signature, a field type or an annotation. Only
  instructions are indexed.
- The code of a later definition of a class that is defined in more than one
  loaded file. Only the first definition, the one a lookup by descriptor
  returns, is indexed, because Android never loads the others.
