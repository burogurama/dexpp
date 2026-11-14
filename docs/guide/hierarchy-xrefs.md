# Class hierarchy and cross-references

Both structures cover the whole program. Each is built the first time you read
it, and kept after that.

## Class hierarchy

`ctx.class_hierarchy` answers questions about inheritance. All queries take
and return descriptors.

```python
--8<-- "hierarchy_xrefs.py:hierarchy"
```

```text
--8<-- "output/hierarchy_xrefs.hierarchy.txt"
```

| Query | Returns |
| ----- | ------- |
| `supertypes(d)` | the direct superclass, then the directly implemented interfaces |
| `all_supertypes(d)` | every ancestor class and interface; `d` itself is not included |
| `subclasses(d)` | the classes that directly extend `d` |
| `implementers(d)` | the classes that directly implement the interface `d` |
| `all_descendants(d)` | everything below `d`, through `extends` and `implements`; `d` itself is not included |
| `is_subtype_of(sub, super)` | whether `sub` is `super` or is below it |
| `is_loaded(d)` | whether the class is defined in a loaded file |
| `is_interface(d)` | whether the class is loaded and is an interface |

Classes outside the loaded files are still in the hierarchy when a loaded
class names them as a superclass or an interface. `Ljava/lang/Object;` is the
usual example. For such a class `is_loaded` is false, and its own ancestors
are unknown: the chain stops there.

A query about a descriptor the hierarchy has never seen returns an empty list,
or `False`.

## Cross-references

`ctx.xrefs` answers "who uses this". It indexes four kinds of use:

| Kind | Query | Indexed instructions |
| ---- | ----- | -------------------- |
| Method | `method_refs(id)`, `method_refs_by_name(name)` | invokes |
| Field | `field_refs(id)`, `field_reads(id)`, `field_writes(id)` | `iget`, `iput`, `sget`, `sput` |
| String | `string_refs(value)` | `const-string` |
| Type | `type_refs(descriptor)` | `const-class`, `check-cast`, `instance-of`, `new-instance`, `new-array`, `filled-new-array` |

Every result is a list of *sites*. A site has a `code_offset` and a
`referrer`: the method whose code contains the instruction.

### Who calls a method

```python
--8<-- "hierarchy_xrefs.py:method_refs"
```

```text
--8<-- "output/hierarchy_xrefs.method_refs.txt"
```

`method_refs_by_name` finds calls by the bare method name, in any class and
with any prototype. It is the quick way to find every call to `exec`,
`loadUrl` or `getInstance`. Each result pairs the exact `MethodId` that was
called with its sites.

`method_refs(id)` takes an exact `MethodId` instead.

Method references are literal: a site is listed under the target that the
bytecode names. A call to `Animal.sound` is not listed under `Dog.sound`. Use
the [call graph](call-graph.md) when you need virtual dispatch.

### The referrer

`site.referrer` is a number, an index into `xrefs.referrers`. There are three
ways to use it:

- `xrefs.referrers[site.referrer]` gives the referrer's `MethodId`. This is a
  view that copies nothing, so it is cheap inside a loop.
- `xrefs.referrer_id(site.referrer)` does the same.
- `xrefs.referrer_method(site.referrer)` gives the `Method`, when you need
  more than the identity.

### Who loads a string

```python
--8<-- "hierarchy_xrefs.py:strings"
```

```text
--8<-- "output/hierarchy_xrefs.strings.txt"
```

`string_refs` matches the whole string exactly. To search by substring or
pattern, go through `referenced_strings` first, then look up the ones that
match:

```python
import re

url: re.Pattern[str] = re.compile(r"https?://")
for value in xrefs.referenced_strings:
    if url.search(value) is not None:
        for site in xrefs.string_refs(value):
            loader: dexpp.MethodId = xrefs.referrers[site.referrer]
            print(value, loader.name)
```

### Who reads or writes a field

```python
--8<-- "hierarchy_xrefs.py:fields"
```

```text
--8<-- "output/hierarchy_xrefs.fields.txt"
```

`field_refs` returns reads and writes together. Each site has `is_write` and
`is_static`.

### Who uses a type

```python
--8<-- "hierarchy_xrefs.py:types"
```

```text
--8<-- "output/hierarchy_xrefs.types.txt"
```

`site.kind` says what the instruction does with the type. For `NEW_ARRAY` the
descriptor is the array type, such as `[I`.

### What is not indexed

- `invoke-custom` call sites.
- The operands of `const-method-handle` and `const-method-type`.
- Types that appear only in a signature, a field type or an annotation. Only
  instructions are indexed.
