# Python reference

Every class and function of the `dexpp` module. The pages are generated from
the type stubs that ship with the package, so they match what your editor and
type checker see.

| Page | Main names |
| ---- | ---------- |
| [APK, manifest, resources, signing](apk.md) | `Apk`, `Manifest`, `Component`, `IntentFilter`, `ComponentKind`, `ResourceTable` and more |
| [Context, classes, methods, fields](analysis.md) | `AnalysisContext`, `Class`, `ClassRef`, `Method`, `Field`, `MethodId` and more |
| [Annotations and values](annotations.md) | `Annotation`, `AnnotationVisibility`, `AnnotationValue`, `TypeRef`, `EnumRef`, `FieldRef` and more |
| [Instructions](instructions.md) | `opcode_name`, `to_string`, `Instruction`, `InstructionBase`, `OperandWidth`, `NopInstruction` and more |
| [Control-flow graph](cfg.md) | `Cfg`, `BasicBlock`, `CfgEdge`, `EdgeKind`, `BasicBlockView` |
| [Call graph](call-graph.md) | `CallGraph`, `CallNode`, `CallEdge`, `InvokeKind`, `EdgeOrigin`, `CallNodeView` and more |
| [Class hierarchy and cross-references](hierarchy-xrefs.md) | `ClassHierarchy`, `Xrefs`, `MethodXref`, `FieldXref`, `StringXref`, `TypeXref` and more |
| [Editable DEX model](model.md) | `Model`, `ClassInfo`, `PoolStats`, `Problem` |
| [Errors](errors.md) | `Error`, `InvalidApkError`, `InvalidDexError`, `UnsupportedDexError`, `ModelError`, `InvalidModelError` and more |

## Conventions

- **Descriptors.** Classes and types are named by DEX descriptors, such as
  `Lcom/example/Foo;`, and `Class.descriptor` gives a class's own one. See [Concepts](../../guide/concepts.md#descriptors).
- **Not found is `None`.** A lookup that finds nothing returns `None`. It does
  not raise.
- **Errors.** A path that does not exist raises the built-in
  `FileNotFoundError`, and one that exists but cannot be read raises `OSError`.
  A file or a model that dexpp cannot use raises one of
  dexpp's own exceptions, which all derive from `dexpp.Error`. See
  [Errors](errors.md).
- **Enums.** `InvokeKind`, `EdgeKind` and the other enums are `enum.Enum`
  classes. Compare members with `is` or `==`, read `kind.name` and
  `kind.value`, and iterate the class to list its members. A member is not a
  number: `int(kind)` raises `TypeError`, and `kind.value` gives the number.
  `AccessFlags` is different: it is an `enum.IntFlag`, so its members are also
  ints.
- **Properties and methods.** A property is cheap or cached: reading it does
  little work, or returns something that was built once and kept, such as
  `cls.methods` or `ctx.call_graph`. A method computes: it does its work again
  on every call, such as `ctx.classes()`, which makes a new object for every
  class, `ctx.strings()`, which copies the whole string table, or
  `model.pool_stats()`, which walks the whole model. Keep the result of a
  method when you need it more than once.
- **Views.** `CallGraph.nodes`, `CallGraph.edges`, `Xrefs.referrers` and
  `Cfg.blocks` and `Method.instructions` are read-only sequences that do not copy the data. They support
  `len()`, indexing, slicing and iteration.
- **Equality.** `Class`, `Method`, `Field` and `ClassRef` objects are equal
  when they point at the same class, method or field of the same analysis, so
  they work as dictionary keys and in sets. One analysis is one
  `AnalysisContext`, or every result of `Apk.analysis()` on one `Apk`. The same
  file loaded twice gives two analyses, and their objects are not equal.
  Identities (`MethodId`, `FieldId`) and the small value classes (`TypeRef`,
  `ResourceName` and the like) are equal when their content is equal.
- **Lifetime.** Every object keeps the analysis it came from alive. You never
  have to keep the `AnalysisContext` or `Apk` around yourself.

The version of the installed package is `dexpp.__version__`.
