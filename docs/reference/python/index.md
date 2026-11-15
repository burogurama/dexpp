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

- **Descriptors.** Class names and types are DEX descriptors, such as
  `Lcom/example/Foo;`. See [Concepts](../../guide/concepts.md#descriptors).
- **Not found is `None`.** A lookup that finds nothing returns `None`. It does
  not raise.
- **Errors.** A path that does not exist raises the built-in
  `FileNotFoundError`, and one that exists but cannot be read raises `OSError`.
  A file or a model that dexpp cannot use raises one of
  dexpp's own exceptions, which all derive from `dexpp.Error`. See
  [Errors](errors.md).
- **Properties.** Most data is exposed as read-only properties, not as methods:
  `cls.methods`, not `cls.methods()`.
- **Views.** `CallGraph.nodes`, `CallGraph.edges`, `Xrefs.referrers` and
  `Cfg.blocks` and `Method.instructions` are read-only sequences that do not copy the data. They support
  `len()`, indexing, slicing and iteration.
- **Lifetime.** Every object keeps the analysis it came from alive. You never
  have to keep the `AnalysisContext` or `Apk` around yourself.

The version of the installed package is `dexpp.__version__`.
