"""Smoke tests for the dexpp Python bindings.

Run from the project root (fixtures are referenced relatively):

    PYTHONPATH=build python3 bindings/python/test_dexpp.py

Also runnable under pytest with the same PYTHONPATH.
"""

import dexpp


def test_load_and_classes():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    names = [c.name for c in ctx.classes]
    assert "LTestDex;" in names

    cls = ctx.find_class("LTestDex;")
    assert cls is not None
    assert cls.is_public
    assert ctx.find_class("LNoSuch;") is None


def test_load_errors():
    try:
        dexpp.AnalysisContext.from_dex("tests/data/nonexistent.dex")
        raise AssertionError("expected FileNotFoundError")
    except FileNotFoundError:
        pass

    try:
        dexpp.AnalysisContext.from_dex("tests/data/TestDex.java")
        raise AssertionError("expected ValueError")
    except ValueError:
        pass


def test_from_apk():
    ctx = dexpp.AnalysisContext.from_apk("tests/data/app.apk")
    assert ctx.find_class("LTestDex;") is not None
    assert ctx.find_class("LFibonacci;") is not None

    try:
        dexpp.AnalysisContext.from_apk("tests/data/nodex.apk")
        raise AssertionError("expected ValueError")
    except ValueError:
        pass

    try:
        dexpp.AnalysisContext.from_apk("tests/data/missing.apk")
        raise AssertionError("expected FileNotFoundError")
    except FileNotFoundError:
        pass


def test_from_apk_threads():
    # multidex.apk has four deflated DEX entries, enough for the parallel path.
    def names(ctx):
        return [c.name for c in ctx.classes]

    serial = dexpp.AnalysisContext.from_apk("tests/data/multidex.apk", threads=1)
    parallel = dexpp.AnalysisContext.from_apk("tests/data/multidex.apk", threads=4)
    automatic = dexpp.AnalysisContext.from_apk("tests/data/multidex.apk")
    assert names(serial)[0] == "LTestDex;"
    assert names(parallel) == names(serial)
    assert names(automatic) == names(serial)


def test_methods_and_instructions():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    methods = {m.name: m for m in cls.methods}
    assert {"<init>", "helloDex", "main"} <= set(methods)

    hello = methods["helloDex"]
    assert hello.is_static and hello.has_code
    assert hello.return_type == "Ljava/lang/String;"
    assert hello.parameters == []

    insns = hello.instructions
    assert isinstance(insns[0], dexpp.ConstStringInstruction)
    assert insns[0].base.offset == 0

    main = methods["main"]
    kinds = [type(i).__name__ for i in main.instructions]
    assert "FieldInstruction" in kinds
    assert "InvokeInstruction" in kinds


def test_cfg():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
    cls = ctx.find_class("LFibonacci;")
    compute = next(m for m in cls.methods if m.name == "compute")
    cfg = compute.cfg
    assert not cfg.empty
    assert len(cfg) > 1
    assert cfg.entry.index == 0
    assert cfg.block_at_offset(0).index == 0
    assert cfg.block_at_offset(99999) is None
    assert [b.index for b in cfg.blocks] == list(range(len(cfg)))
    assert all(cfg.blocks[e.target_block].index == e.target_block
               for b in cfg.blocks for e in b.successors)
    kinds = {e.kind for b in cfg.blocks for e in b.successors}
    assert dexpp.EdgeKind.BRANCH_TAKEN in kinds or dexpp.EdgeKind.GOTO in kinds


def test_call_graph():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    g = ctx.call_graph
    assert not g.empty

    sound = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")
    node = g.find(sound)
    assert node is not None and node.resolved
    assert g.find(dexpp.MethodId("LNo;", "where", "()V")) is None

    # CHA: interface call site fans out to overrides in Dog and Cat.
    override_targets = set()
    for e in g.edges:
        if e.origin == dexpp.EdgeOrigin.CHA_OVERRIDE:
            override_targets.add(g.nodes[e.callee_node].id.class_descriptor)
    assert {"LDog;", "LCat;"} <= override_targets

    # Upward resolution: Pet defines no sound(), so Pet.describe's call site
    # resolves to the abstract Animal.sound declaration.
    inherited = [
        e for e in g.edges
        if e.origin == dexpp.EdgeOrigin.INHERITED_RESOLUTION
        and g.nodes[e.caller_node].id.class_descriptor == "LPet;"
    ]
    assert len(inherited) == 1
    assert g.nodes[inherited[0].callee_node].id.class_descriptor == "LAnimal;"
    assert g.nodes[inherited[0].callee_node].resolved

    # Every call site has exactly one declared edge.
    declared = [e for e in g.edges if e.origin == dexpp.EdgeOrigin.DECLARED]
    sites = {(e.caller_node, e.code_offset) for e in declared}
    assert len(declared) == len(sites)

    # Deprecated alias: True only for CHA_OVERRIDE edges, and warns.
    import warnings

    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        cha_edge = next(e for e in g.edges if e.origin == dexpp.EdgeOrigin.CHA_OVERRIDE)
        assert cha_edge.via_override is True
        assert inherited[0].via_override is False
        assert any(issubclass(w.category, DeprecationWarning) for w in caught)

    m = g.method_for(node)
    assert m is not None and m.name == "sound" and not m.has_code


def test_call_graph_views_and_handles():
    import collections.abc
    import gc

    ctx = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    g = ctx.call_graph
    nodes, edges = g.nodes, g.edges

    # Zero-copy views behave as read-only sequences.
    assert isinstance(nodes, collections.abc.Sequence)
    assert isinstance(edges, collections.abc.Sequence)
    assert len(nodes) > 0 and len(edges) > 0
    assert nodes[0].index == 0
    assert nodes[-1].index == len(nodes) - 1
    assert [n.index for n in nodes[1:3]] == [1, 2]
    assert [n.index for n in nodes] == list(range(len(nodes)))
    for bad in (len(nodes), -len(nodes) - 1):
        try:
            nodes[bad]
            raise AssertionError("expected IndexError")
        except IndexError:
            pass

    # Handles navigate directly and agree with the index fields.
    for e in edges:
        assert e.caller == nodes[e.caller_node]
        assert e.callee == nodes[e.callee_node]
        assert e in e.caller.out_edges and e in e.callee.in_edges
    node = g.find(dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;"))
    assert list(node.in_edges) == [edges[i] for i in node.incoming]
    assert list(node.out_edges) == [edges[i] for i in node.outgoing]
    assert node.method is not None and node.method.name == "sound"
    assert g.method_for(node).name == "sound"
    external = next(n for n in nodes if not n.resolved)
    assert external.method is None

    # Handles are hashable and compare by identity within the graph.
    assert len({nodes[0], nodes[0], g.nodes[0]}) == 1
    assert len(set(edges)) == len(edges)

    # where() filters in C++ and composes with node edge views.
    declared = edges.where(origin=dexpp.EdgeOrigin.DECLARED)
    assert list(declared) == [e for e in edges if e.origin == dexpp.EdgeOrigin.DECLARED]
    cha_interface = edges.where(
        origin=dexpp.EdgeOrigin.CHA_OVERRIDE, kind=dexpp.InvokeKind.INTERFACE
    )
    assert all(
        e.origin == dexpp.EdgeOrigin.CHA_OVERRIDE and e.kind == dexpp.InvokeKind.INTERFACE
        for e in cha_interface
    )
    assert len(edges.where()) == len(edges)
    assert list(node.in_edges.where(origin=dexpp.EdgeOrigin.DECLARED)) == [
        e for e in node.in_edges if e.origin == dexpp.EdgeOrigin.DECLARED
    ]

    # A view keeps the analysis alive after every other reference is gone.
    view = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex").call_graph.nodes
    gc.collect()
    assert view[0].id.name


def test_class_hierarchy():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    h = ctx.class_hierarchy
    assert h.is_subtype_of("LDog;", "LAnimal;")
    assert not h.is_subtype_of("LAnimal;", "LDog;")
    assert set(h.all_descendants("LAnimal;")) == {"LPet;", "LDog;", "LCat;"}
    assert h.is_interface("LAnimal;")
    assert not h.is_loaded("Ljava/lang/Object;")


def test_xrefs():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    x = ctx.xrefs
    assert not x.empty

    sites = x.string_refs("Hello Dex++!!")
    assert len(sites) == 1
    assert x.referrer_id(sites[0].referrer).name == "helloDex"
    assert x.referrer_method(sites[0].referrer).name == "helloDex"
    assert "Hello Dex++!!" in x.referenced_strings
    # referrers is a zero-copy view: indexing it per site is cheap.
    assert x.referrers[sites[0].referrer] == x.referrer_id(sites[0].referrer)
    assert len(x.referrers) == len(list(x.referrers))

    out = dexpp.FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;")
    refs = x.field_refs(out)
    assert len(refs) == 1 and refs[0].is_static and not refs[0].is_write
    assert len(x.field_reads(out)) == 1
    assert x.field_writes(out) == []

    by_name = x.method_refs_by_name("println")
    assert len(by_name) == 1
    target, call_sites = by_name[0]
    assert target.class_descriptor == "Ljava/io/PrintStream;"
    assert call_sites[0].kind == dexpp.InvokeKind.VIRTUAL


def test_mnemonics():
    assert dexpp.opcode_name(0x00) == "nop"
    assert dexpp.opcode_name(0x6e) == "invoke-virtual"
    assert dexpp.opcode_name(0x73) == "unused-73"

    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    hello = next(m for m in cls.methods if m.name == "helloDex")
    rendered = [dexpp.to_string(i) for i in hello.instructions]
    assert any(r.startswith("const-string v") and "string@" in r for r in rendered)
    assert any(r.startswith("return-object v") for r in rendered)


def test_strings_enumeration():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    strings = ctx.strings
    assert "Hello Dex++!!" in strings
    assert "LTestDex;" in strings  # type name, not bytecode-loaded
    for ref in ctx.xrefs.referenced_strings:
        assert ref in strings


def test_forward_refs():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    main = next(m for m in cls.methods if m.name == "main")

    assert any(c.target.name == "helloDex" and c.kind == dexpp.InvokeKind.STATIC
               for c in main.calls)
    assert any(c.target.name == "println" and c.kind == dexpp.InvokeKind.VIRTUAL
               for c in main.calls)

    reads = main.field_accesses
    assert len(reads) == 1
    assert reads[0].field.name == "out" and reads[0].is_static and not reads[0].is_write

    hello = next(m for m in cls.methods if m.name == "helloDex")
    loads = hello.string_loads
    assert len(loads) == 1 and loads[0].value == "Hello Dex++!!"


def test_parameter_registers():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
    cls = ctx.find_class("LFibonacci;")

    compute = next(m for m in cls.methods if m.name == "compute")  # instance, int param
    assert compute.register_count == 11
    pr = compute.parameter_registers
    assert len(pr) == 2
    assert pr[0].is_this and pr[0].reg == 9 and pr[0].type == "LFibonacci;"
    assert not pr[1].is_wide and pr[1].reg == 10 and pr[1].type == "I"

    classify = next(m for m in cls.methods if m.name == "classify")  # static, long param
    cp = classify.parameter_registers
    assert len(cp) == 1
    assert cp[0].is_wide and cp[0].reg == 3 and cp[0].type == "J"
    assert not cp[0].is_this


def test_try_blocks():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/trycatch.dex")
    cls = ctx.find_class("LTryCatch;")
    divide = next(m for m in cls.methods if m.name == "divide")

    tries = divide.try_blocks
    assert len(tries) == 1
    assert tries[0].start_offset < tries[0].end_offset
    assert len(tries[0].handlers) == 1
    assert tries[0].handlers[0].type_descriptor == "Ljava/lang/ArithmeticException;"

    safe = next(m for m in cls.methods if m.name == "safeParse")
    assert any(t.catch_all_offset is not None for t in safe.try_blocks)


def test_annotations():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
    cls = ctx.find_class("LAnnotated;")

    marker = next(a for a in cls.annotations if a.type_descriptor == "LMarker;")
    assert marker.visibility == dexpp.AnnotationVisibility.RUNTIME
    elems = marker.elements
    assert elems["name"] == "cls"
    assert elems["level"] == 3
    assert elems["tags"] == ["a", "b"]
    assert "target" not in elems  # left at default

    greet = next(m for m in cls.methods if m.name == "greet")
    gmarker = next(a for a in greet.annotations if a.type_descriptor == "LMarker;")
    assert gmarker.elements["target"].descriptor == "Ljava/lang/String;"

    old = next(m for m in cls.methods if m.name == "old")
    assert any(a.type_descriptor == "Ljava/lang/Deprecated;" for a in old.annotations)

    magic = next(f for f in cls.fields if f.name == "MAGIC")
    fmarker = next(a for a in magic.annotations if a.type_descriptor == "LMarker;")
    assert fmarker.elements["name"] == "fld"


def test_initial_values():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
    cls = ctx.find_class("LAnnotated;")
    fields = {f.name: f for f in cls.fields}

    assert fields["MAGIC"].initial_value == 42
    assert fields["GREETING"].initial_value == "hello"
    assert fields["BIG"].initial_value == 1234567890123
    assert fields["PI"].initial_value == 3.25
    assert fields["FLAG"].initial_value is True
    assert fields["counter"].initial_value is None


def test_apk_and_manifest():
    apk = dexpp.Apk.open("tests/data/sample.apk")
    assert "classes.dex" in apk.entries
    assert apk.read("classes.dex") is not None
    assert apk.read("nope") is None

    m = apk.manifest
    assert m is not None
    assert m.package == "com.example.dexpp"
    assert m.version_code == 7
    assert m.version_name == "1.2.3"
    assert m.min_sdk == 21 and m.target_sdk == 34
    assert "android.permission.INTERNET" in m.permissions
    assert m.declared_permissions == ["com.example.dexpp.CUSTOM"]
    assert m.debuggable is True
    assert m.allow_backup is False
    assert m.launcher_activity == "com.example.dexpp.MainActivity"

    activities = m.components_of(dexpp.ComponentKind.ACTIVITY)
    assert len(activities) == 1
    assert activities[0].is_exported
    assert activities[0].intent_filters[0].is_launcher

    service = next(c for c in m.components if c.name.endswith("SyncService"))
    assert not service.is_exported


def test_reachability():
    apk = dexpp.Apk.open("tests/data/reachable.apk")
    m = apk.manifest
    assert m is not None

    assert m.declared_permission_protection_level("com.dexpp.reach.P_NORMAL") == 0
    assert m.declared_permission_protection_level("com.dexpp.reach.P_DANGEROUS") == 1
    assert m.declared_permission_protection_level("com.dexpp.reach.P_SIGNATURE") == 2
    # Undeclared (e.g. platform) permission -> unknown.
    assert m.declared_permission_protection_level("android.permission.DUMP") is None

    by_name = {c.name.split(".")[-1]: c for c in m.components}
    # Exported + unguarded / normal / dangerous / undeclared-platform perm -> reachable.
    assert by_name["OpenActivity"].is_reachable
    assert by_name["NormalActivity"].is_reachable
    assert by_name["DangerousActivity"].is_reachable
    assert by_name["PlatformPermActivity"].is_reachable
    assert by_name["OpenReceiver"].is_reachable
    # Exported but guarded by an app-declared signature permission -> not reachable.
    assert by_name["SigActivity"].is_exported
    assert by_name["SigActivity"].guarded_by_signature_permission
    assert not by_name["SigActivity"].is_reachable
    assert by_name["SigService"].is_exported and not by_name["SigService"].is_reachable
    # Not exported -> not reachable.
    assert not by_name["HiddenActivity"].is_exported
    assert not by_name["HiddenActivity"].is_reachable

    activities = m.components_of(dexpp.ComponentKind.ACTIVITY)
    assert sum(c.is_exported for c in activities) == 5
    assert sum(c.is_reachable for c in activities) == 4


def test_resources():
    apk = dexpp.Apk.open("tests/data/sample.apk")
    rt = apk.resources
    assert rt is not None
    assert not rt.empty

    app_name = rt.id_of("string", "app_name")
    assert app_name is not None
    assert (app_name >> 24) & 0xFF == 0x7F
    assert rt.resolve_string(app_name) == "Dexpp Sample"

    name = rt.name_of(app_name)
    assert name.package == "com.example.dexpp"
    assert name.type == "string" and name.entry == "app_name"

    assert rt.id_of("string", "nope") is None
    assert rt.resolve_string(0x7F999999) is None


def test_apk_resolve_string():
    apk = dexpp.Apk.open("tests/data/sample.apk")
    ref = apk.resources.id_of("string", "app_name")
    assert apk.resolve_string(ref) == "Dexpp Sample"
    assert apk.resolve_string(0x7F999999) is None


def test_signing():
    apk = dexpp.Apk.open("tests/data/signed.apk")
    s = apk.signing
    assert s.is_signed
    assert s.v1 and s.v2 and s.v3
    assert len(s.certificates) == 1

    cert = s.certificates[0]
    assert cert.subject == "C=MA, O=DexppOrg, CN=Dexpp Test"
    assert cert.self_signed
    assert cert.serial_hex == "ba51e610667560a3"
    assert cert.sha256_hex == "6c9647205d1ae141f9104587153cb1c71c5da9251335da1a1977de500c6ef599"
    assert isinstance(cert.der, bytes) and len(cert.der) > 0

    unsigned = dexpp.Apk.open("tests/data/sample.apk")
    assert not unsigned.signing.is_signed
    assert unsigned.signing.certificates == []


def test_apk_analysis_bridge():
    apk = dexpp.Apk.open("tests/data/sample.apk")
    ctx = apk.analysis()
    assert ctx.find_class("LTestDex;") is not None


def test_apk_open_errors():
    try:
        dexpp.Apk.open("tests/data/missing.apk")
        raise AssertionError("expected FileNotFoundError")
    except FileNotFoundError:
        pass
    try:
        dexpp.Apk.open("tests/data/classes.dex")
        raise AssertionError("expected ValueError")
    except ValueError:
        pass


def test_handles_outlive_context():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    del ctx
    # The Class handle holds its own shared_ptr to the impl.
    assert cls.name == "LTestDex;"
    assert [m.name for m in cls.methods]


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"PASS {t.__name__}")
    print(f"{len(tests)} tests passed")


if __name__ == "__main__":
    main()
