"""Smoke tests for the dexpp Python bindings.

Run from the project root (fixtures are referenced relatively):

    PYTHONPATH=build python3 bindings/python/test_dexpp.py

Also runnable under pytest with the same PYTHONPATH.
"""

import ast
import enum
import inspect
import os
import pickle
import re
import tempfile
import types

import dexpp


def test_version():
    # src/version.hpp is the single source of truth for the version.
    with open("src/version.hpp") as f:
        expected = re.search(r'#define DEXPP_VERSION "([^"]+)"', f.read()).group(1)
    assert dexpp.__version__ == expected


def test_load_and_classes():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    names = [c.descriptor for c in ctx.classes()]
    assert "LTestDex;" in names
    # A method makes a new list, of equal classes, on each call.
    classes = ctx.classes()
    assert ctx.classes() == classes and ctx.classes() is not classes

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
        return [c.descriptor for c in ctx.classes()]

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


def test_instruction_view():
    import collections.abc
    import gc

    ctx = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
    compute = next(m for m in ctx.find_class("LFibonacci;").methods if m.name == "compute")
    view = compute.instructions
    assert isinstance(view, dexpp.InstructionView)
    assert isinstance(view, collections.abc.Sequence)
    assert len(view) > 3
    assert view[0].base.offset == 0
    assert view[-1].base.offset == view[len(view) - 1].base.offset
    offsets = [i.base.offset for i in view]
    assert offsets == sorted(offsets)
    assert [i.base.offset for i in view[1:3]] == offsets[1:3]
    assert isinstance(view[1:3], list)
    for bad in (len(view), -len(view) - 1):
        try:
            view[bad]
            raise AssertionError("expected IndexError")
        except IndexError:
            pass
    # Every read gives the same instructions.
    assert [dexpp.to_string(i) for i in view] == [
        dexpp.to_string(i) for i in compute.instructions
    ]

    # A method without code has an empty sequence.
    pets = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    abstract = next(m for m in pets.find_class("LAnimal;").methods if m.name == "sound")
    assert not abstract.has_code and len(abstract.instructions) == 0

    # The view keeps the analysis alive on its own.
    alone = dexpp.AnalysisContext.from_dex("tests/data/classes.dex").find_class(
        "LTestDex;"
    ).methods[1].instructions
    gc.collect()
    assert len(alone) > 0 and dexpp.to_string(alone[0])


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

    # CHA: the interface call may dispatch to the Dog and Cat overrides.  The
    # targets are stored once on the declared node and shared by its sites.
    assert {t.id.class_descriptor for t in node.dispatch_targets} == {"LDog;", "LCat;"}
    noise = g.find(dexpp.MethodId("LKennel;", "noise", "(LAnimal;)Ljava/lang/String;"))
    site = next(e for e in noise.out_edges if e.callee == node)
    assert site.origin == dexpp.EdgeOrigin.DECLARED
    assert site.kind == dexpp.InvokeKind.INTERFACE
    assert list(site.dispatch_targets) == list(node.dispatch_targets)
    assert not hasattr(dexpp.EdgeOrigin, "CHA_OVERRIDE")

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

    # A site's dispatch targets hang off its declared edge only.
    assert len(inherited[0].dispatch_targets) == 0

    # The reverse direction and the neighbour helpers include dispatch.
    dog = g.find(dexpp.MethodId("LDog;", "sound", "()Ljava/lang/String;"))
    assert {s.id.class_descriptor for s in dog.dispatch_sources} == {"LAnimal;", "LPet;"}
    assert {c.id.name for c in dog.callers()} == {"noise", "dogNoise", "describe"}
    assert {c.id.class_descriptor for c in noise.callees()} == {"LAnimal;", "LDog;", "LCat;"}
    # Each call computes a new view with the same nodes.
    assert list(dog.callers()) == list(dog.callers())
    assert list(noise.callees()) == list(noise.callees())

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
    inherited_virtual = edges.where(
        origin=dexpp.EdgeOrigin.INHERITED_RESOLUTION, kind=dexpp.InvokeKind.VIRTUAL
    )
    assert len(inherited_virtual) > 0
    assert all(
        e.origin == dexpp.EdgeOrigin.INHERITED_RESOLUTION and e.kind == dexpp.InvokeKind.VIRTUAL
        for e in inherited_virtual
    )
    assert len(edges.where()) == len(edges)
    assert list(node.in_edges.where(origin=dexpp.EdgeOrigin.DECLARED)) == [
        e for e in node.in_edges if e.origin == dexpp.EdgeOrigin.DECLARED
    ]

    # Node subsets — dispatch lists, callers, callees — are sequence views too.
    for subset in (node.dispatch_targets, node.dispatch_sources, node.callers(), node.callees()):
        assert isinstance(subset, dexpp.CallNodeView)
        assert isinstance(subset, collections.abc.Sequence)
        indices = [n.index for n in subset]
        assert indices == sorted(set(indices))
        if indices:
            assert subset[-1] == subset[len(subset) - 1]
            assert [n.index for n in subset[:1]] == indices[:1]

    # A view keeps the analysis alive after every other reference is gone.
    view = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex").call_graph.nodes
    gc.collect()
    assert view[0].id.name
    sound = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")
    targets = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex").call_graph.find(
        sound
    ).dispatch_targets
    callers = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex").call_graph.find(
        sound
    ).callers()
    gc.collect()
    assert {t.id.class_descriptor for t in targets} == {"LDog;", "LCat;"}
    assert {c.id.name for c in callers} == {"noise", "describe"}


def test_call_graph_does_not_depend_on_thread_count():
    def describe(g):
        return (
            [(n.id.class_descriptor, n.id.name, n.id.proto, n.resolved) for n in g.nodes],
            [(e.caller_node, e.callee_node, e.code_offset, e.kind, e.origin) for e in g.edges],
            [[t.index for t in n.dispatch_targets] for n in g.nodes],
        )

    path = "tests/data/multidex.apk"
    one = dexpp.AnalysisContext.from_apk(path, threads=1).call_graph
    automatic = dexpp.AnalysisContext.from_apk(path).call_graph
    assert len(one.edges) > 0
    assert describe(one) == describe(automatic)


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
    assert "Hello Dex++!!" in x.referenced_strings()
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
    strings = ctx.strings()
    assert "Hello Dex++!!" in strings
    assert "LTestDex;" in strings  # type name, not bytecode-loaded
    for ref in ctx.xrefs.referenced_strings():
        assert ref in strings
    # A method makes a new list on each call.
    assert ctx.strings() == strings and ctx.strings() is not strings


def test_forward_refs():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    main = next(m for m in cls.methods if m.name == "main")

    calls = main.calls()
    assert any(c.target.name == "helloDex" and c.kind == dexpp.InvokeKind.STATIC
               for c in calls)
    assert any(c.target.name == "println" and c.kind == dexpp.InvokeKind.VIRTUAL
               for c in calls)

    reads = main.field_accesses()
    assert len(reads) == 1
    assert reads[0].field.name == "out" and reads[0].is_static and not reads[0].is_write

    hello = next(m for m in cls.methods if m.name == "helloDex")
    loads = hello.string_loads()
    assert len(loads) == 1 and loads[0].value == "Hello Dex++!!"

    # Each call walks the bytecode again and makes a new list with equal entries.
    again = main.calls()
    assert again is not calls and len(again) == len(calls)
    assert [(c.target, c.kind, c.code_offset) for c in again] == [
        (c.target, c.kind, c.code_offset) for c in calls
    ]
    assert hello.string_loads()[0].value == loads[0].value

    # Dog.describe builds its result with a StringBuilder (super.describe() + "!").
    pets = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    describe = next(m for m in pets.find_class("LDog;").methods if m.name == "describe")
    uses = describe.type_uses()
    assert ("Ljava/lang/StringBuilder;", dexpp.TypeRefKind.NEW_INSTANCE) in [
        (u.descriptor, u.kind) for u in uses
    ]


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
    assert "classes.dex" in apk.entries()
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
    assert cls.descriptor == "LTestDex;"
    assert [m.name for m in cls.methods]


def test_handles_are_equal_when_they_point_at_the_same_thing():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/shapes.dex")
    circle = ctx.find_class("LCircle;")
    rectangle = ctx.find_class("LRectangle;")
    assert circle == ctx.find_class("LCircle;")
    assert hash(circle) == hash(ctx.find_class("LCircle;"))
    assert circle != rectangle
    assert circle in ctx.classes()
    assert len(set(ctx.classes()) | set(ctx.classes())) == len(ctx.classes())

    methods = circle.methods
    assert methods[0] == circle.methods[0] and methods[0] != methods[1]
    assert len({*methods, *circle.methods}) == len(methods)
    names = {method: method.name for method in methods}
    assert names[circle.methods[-1]] == methods[-1].name
    fields = rectangle.fields
    assert len({*fields, *ctx.find_class("LRectangle;").fields}) == len(fields) == 2

    # ClassRef: the same name in the same analysis.
    parent = circle.superclass
    assert parent == rectangle.superclass and hash(parent) == hash(rectangle.superclass)
    assert parent != ctx.find_class("LAbstractShape;").interfaces[0]
    assert methods[0].declaring_class == circle.fields[0].declaring_class

    # Different kinds of object are never equal, even with the same name.
    assert parent != ctx.find_class("LAbstractShape;")
    assert circle != "LCircle;" and methods[0] != circle

    # The same file loaded twice gives two analyses.
    other = dexpp.AnalysisContext.from_dex("tests/data/shapes.dex")
    assert other.find_class("LCircle;") != circle
    assert other.find_class("LCircle;").methods[0] != methods[0]
    assert other.find_class("LCircle;").superclass != parent

    # Apk.analysis() is built once, so its results are one analysis.
    apk = dexpp.Apk.open("tests/data/sample.apk")
    assert apk.analysis().find_class("LTestDex;") == apk.analysis().find_class("LTestDex;")


def test_every_class_with_equality_is_hashable():
    # pybind11 makes a class that defines __eq__ without __hash__ unhashable.
    for name, value in vars(dexpp).items():
        if isinstance(value, type) and "__eq__" in vars(value):
            assert vars(value).get("__hash__") is not None, name

    # The value records hash by their content.
    annotated = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
    def element(method_name):
        method = next(m for m in annotated.find_class("LAnnotated;").methods if m.name == method_name)
        return next(a for a in method.annotations if a.type_descriptor == "LMarker;")
    target = element("greet").elements["target"]
    assert isinstance(target, dexpp.TypeRef)
    assert target == element("greet").elements["target"]
    assert len({target, element("greet").elements["target"]}) == 1

    retention = next(
        a for a in annotated.find_class("LMarker;").annotations
        if a.type_descriptor == "Ljava/lang/annotation/Retention;"
    )
    policy = retention.elements["value"]
    assert isinstance(policy, dexpp.EnumRef) and policy.constant.name == "RUNTIME"
    assert hash(policy) == hash(retention.elements["value"])

    resources = dexpp.Apk.open("tests/data/sample.apk").resources
    app_name = resources.id_of("string", "app_name")
    assert resources.name_of(app_name) == resources.name_of(app_name)
    assert len({resources.name_of(app_name), resources.name_of(app_name)}) == 1


# ===== editable DEX model (dex::model) =====

# shapes.dex: interface LShape;, abstract LAbstractShape; implementing it, and
# concrete subclasses LCircle;, LRectangle;.


def test_model_read_and_classes():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    assert m.class_count == len(m) > 0
    assert m.class_descriptors() == [
        "LShape;",
        "LAbstractShape;",
        "LCircle;",
        "LRectangle;",
    ]
    assert m.has_class("LCircle;")
    assert "LCircle;" in m
    assert not m.has_class("Lno/such/Class;")
    assert "Lno/such/Class;" not in m


def test_model_class_info():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    circle = m.class_info("LCircle;")
    assert circle is not None
    assert circle.descriptor == "LCircle;"
    assert circle.superclass == "LAbstractShape;"
    assert circle.method_count > 0
    assert m.class_interfaces("LAbstractShape;") == ["LShape;"]
    # An interface's own superclass is java.lang.Object.
    shape = m.class_info("LShape;")
    assert shape.superclass == "Ljava/lang/Object;"
    assert m.class_info("Lno/such/Class;") is None
    assert m.class_interfaces("Lno/such/Class;") == []


def test_model_write_round_trip():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    data = m.write()
    assert isinstance(data, bytes) and len(data) > 0
    again = dexpp.Model.read(data)
    assert again.class_count == m.class_count
    # write(read(write(m))) is a byte fixpoint.
    assert again.write() == data


def test_model_split_one_per_class():
    src = dexpp.Model.read_file("tests/data/shapes.dex")
    descriptors = src.class_descriptors()
    work = src.clone()
    for d in descriptors:
        one = dexpp.Model(src.version)
        one.move_class(work, d)
        part = dexpp.Model.read(one.write())
        assert part.class_descriptors() == [d]
    assert work.class_count == 0
    # The clone was drained; the original is untouched.
    assert src.class_count == len(descriptors)


def test_model_copy_remove_and_errors():
    src = dexpp.Model.read_file("tests/data/shapes.dex")
    dst = dexpp.Model(src.version)
    dst.copy_class(src, "LCircle;")
    assert src.has_class("LCircle;")  # copy left the source intact
    assert dst.has_class("LCircle;")

    for bad in (
        lambda: dst.copy_class(src, "LCircle;"),  # destination already has it
        lambda: dst.move_class(src, "Lno/such/Class;"),  # source has no such class
    ):
        try:
            bad()
            raise AssertionError("expected ValueError")
        except ValueError:
            pass

    assert dst.remove_class("LCircle;")
    assert not dst.remove_class("LCircle;")
    assert dst.class_count == 0


def test_model_rename_types():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    m.rename_types({"LCircle;": "Lcom/x/Round;"})
    assert not m.has_class("LCircle;")
    assert m.has_class("Lcom/x/Round;")
    # The rename reaches references too, and the model still writes.
    assert len(m.write()) > 0


def test_model_validate_and_stats():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    assert m.validate() == []
    stats = m.pool_stats()
    assert stats.strings > 0 and stats.types > 0 and stats.methods > 0
    assert m.required_version() >= 35


def test_model_new_and_version():
    m = dexpp.Model(39)
    assert m.version == 39
    assert m.class_count == 0
    m.version = 35
    assert m.version == 35
    # An empty model writes to a valid, re-readable DEX.
    dexpp.Model.read(m.write())


def test_model_write_file():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "out.dex")
        m.write_file(path)
        assert os.path.getsize(path) > 0
        reread = dexpp.Model.read_file(path)
        assert reread.class_count == m.class_count


def test_model_read_accepts_any_bytes_like():
    with open("tests/data/shapes.dex", "rb") as f:
        data = f.read()
    expected = dexpp.Model.read(data).write()

    assert dexpp.Model.read(bytearray(data)).write() == expected
    assert dexpp.Model.read(memoryview(data)).write() == expected

    # A view whose bytes are not next to each other in memory: every byte of
    # the DEX appears twice in `doubled`, and the view takes every other one.
    doubled = bytes(byte for byte in data for _ in range(2))
    strided = memoryview(doubled)[::2]
    assert strided.c_contiguous is False
    assert dexpp.Model.read(strided).write() == expected

    # A reversed view is not a DEX file: it is refused, and nothing is read
    # past the end of the buffer.
    try:
        dexpp.Model.read(memoryview(data)[::-1])
        raise AssertionError("expected InvalidDexError")
    except dexpp.InvalidDexError:
        pass


def test_model_errors():
    try:
        dexpp.Model.read(b"not a dex")
        raise AssertionError("expected ValueError")
    except ValueError:
        pass
    try:
        dexpp.Model.read_file("tests/data/does_not_exist.dex")
        raise AssertionError("expected FileNotFoundError")
    except FileNotFoundError:
        pass


# ===== errors =====


def _error_from(action):
    """The dexpp or file error that `action` raises; fails if it raises none."""
    try:
        action()
    except (dexpp.Error, OSError) as error:
        return error
    raise AssertionError("expected an error")


def test_error_hierarchy():
    assert issubclass(dexpp.Error, ValueError)
    for direct in (
        dexpp.InvalidApkError,
        dexpp.InvalidDexError,
        dexpp.UnsupportedDexError,
        dexpp.ModelError,
    ):
        assert direct.__bases__ == (dexpp.Error,)
    for model_error in (
        dexpp.InvalidModelError,
        dexpp.PoolOverflowError,
        dexpp.VersionTooLowError,
    ):
        assert model_error.__bases__ == (dexpp.ModelError,)
    # The classes belong to the module, like every other name in it.
    assert dexpp.InvalidModelError.__module__ == "dexpp"


def test_errors_are_still_value_errors():
    # Code written before the specific classes existed catches ValueError.
    for action in (
        lambda: dexpp.AnalysisContext.from_dex("tests/data/TestDex.java"),
        lambda: dexpp.AnalysisContext.from_apk("tests/data/nodex.apk"),
        lambda: dexpp.Apk.open("tests/data/classes.dex"),
        lambda: dexpp.Model.read(b"not a dex"),
    ):
        try:
            action()
            raise AssertionError("expected ValueError")
        except ValueError:
            pass


def test_invalid_dex_error():
    not_a_dex = "tests/data/TestDex.java"
    for action in (
        lambda: dexpp.AnalysisContext.from_dex(not_a_dex),
        lambda: dexpp.AnalysisContext.from_dex_files(["tests/data/classes.dex", not_a_dex]),
        lambda: dexpp.Model.read(b"not a dex"),
        lambda: dexpp.Model.read_file(not_a_dex),
    ):
        error = _error_from(action)
        assert type(error) is dexpp.InvalidDexError
        assert str(error) != ""


def test_invalid_apk_error():
    for action in (
        lambda: dexpp.AnalysisContext.from_apk("tests/data/nodex.apk"),  # no classes.dex
        lambda: dexpp.AnalysisContext.from_apk("tests/data/classes.dex"),  # not a ZIP
        lambda: dexpp.Apk.open("tests/data/classes.dex"),
        lambda: dexpp.Apk.open("tests/data/nodex.apk").analysis(),
    ):
        assert type(_error_from(action)) is dexpp.InvalidApkError


def test_unsupported_dex_error():
    # The model does not read the DEX version 41 container format.
    with open("tests/data/shapes.dex", "rb") as f:
        data = bytearray(f.read())
    data[4:7] = b"041"
    error = _error_from(lambda: dexpp.Model.read(bytes(data)))
    assert type(error) is dexpp.UnsupportedDexError


def test_model_error_for_copy_and_move():
    src = dexpp.Model.read_file("tests/data/shapes.dex")
    dst = dexpp.Model(src.version)
    dst.copy_class(src, "LCircle;")
    for action in (
        lambda: dst.copy_class(src, "LCircle;"),  # destination already has it
        lambda: dst.move_class(src, "LCircle;"),
        lambda: dst.copy_class(src, "Lno/such/Class;"),  # source has no such class
        lambda: dst.move_class(src, "Lno/such/Class;"),
    ):
        assert type(_error_from(action)) is dexpp.ModelError


def test_invalid_model_error_lists_problems():
    model = dexpp.Model.read_file("tests/data/shapes.dex")
    model.rename_types({"LCircle;": "not a descriptor"})
    expected = [(p.where, p.message) for p in model.validate()]
    assert len(expected) > 0

    for action in (model.write, lambda: model.write_file(os.devnull)):
        error = _error_from(action)
        assert type(error) is dexpp.InvalidModelError
        assert [(p.where, p.message) for p in error.problems] == expected
        assert all(isinstance(p, dexpp.Problem) for p in error.problems)
        assert "not a descriptor" in str(error)

    # Made by hand, the error has no problems unless they are given.
    assert dexpp.InvalidModelError("broken").problems == []
    assert len(dexpp.InvalidModelError("broken", error.problems).problems) == len(expected)


def test_pool_overflow_error():
    # A DEX file holds at most 65536 fields.  The class in this fixture has 7
    # fields of its own, so 9400 copies of it, each renamed into its own
    # package, need more than that.
    source = dexpp.Model.read_file("tests/data/multistring.dex", debug_info=False)
    merged = dexpp.Model(source.version)
    for copy in range(9400):
        part = source.clone()
        part.rename_types({d: f"Lcopy{copy}/{d[1:]}" for d in part.class_descriptors()})
        for descriptor in part.class_descriptors():
            merged.move_class(part, descriptor)
    assert merged.pool_stats().fields > 65536

    error = _error_from(merged.write)
    assert type(error) is dexpp.PoolOverflowError
    assert "65536" in str(error)


def test_version_too_low_error():
    # inherit.dex has default interface methods, which need DEX version 37.
    model = dexpp.Model.read_file("tests/data/inherit.dex")
    assert model.required_version() == 37
    model.version = 35
    assert type(_error_from(model.write)) is dexpp.VersionTooLowError

    model.version = model.required_version()
    assert len(model.write()) > 0


def test_file_errors_are_not_dexpp_errors():
    model = dexpp.Model.read_file("tests/data/shapes.dex")
    with tempfile.TemporaryDirectory() as directory:
        missing = os.path.join(directory, "missing.dex")
        for action in (
            lambda: dexpp.AnalysisContext.from_dex(missing),
            lambda: dexpp.AnalysisContext.from_apk(missing),
            lambda: dexpp.Apk.open(missing),
            lambda: dexpp.Model.read_file(missing),
        ):
            assert type(_error_from(action)) is FileNotFoundError

        # The path exists but is not a readable file: not "file not found",
        # and never a crash or a MemoryError.
        for action in (
            lambda: dexpp.AnalysisContext.from_dex(directory),
            lambda: dexpp.AnalysisContext.from_dex_files(["tests/data/classes.dex", directory]),
            lambda: dexpp.AnalysisContext.from_apk(directory),
            lambda: dexpp.Apk.open(directory),
            lambda: dexpp.Model.read_file(directory),
        ):
            error = _error_from(action)
            assert type(error) is OSError
            assert "directory" in str(error)
        # A file that cannot be created.
        unwritable = os.path.join(directory, "no", "such", "folder", "out.dex")
        assert type(_error_from(lambda: model.write_file(unwritable))) is OSError
        assert type(_error_from(lambda: model.write_file(directory))) is OSError
        # A full disk: on Linux every write to /dev/full fails, but only when
        # the file is closed, because the bytes fit in one buffer.
        if os.path.exists("/dev/full"):
            assert type(_error_from(lambda: model.write_file("/dev/full"))) is OSError


# ===== the type stubs =====

STUB_PATH = "bindings/python/dexpp.pyi"


def _has_docstring(body, index):
    """Whether the statement after body[index] is a string: an attribute's docstring."""
    if index + 1 >= len(body):
        return False
    following = body[index + 1]
    return (
        isinstance(following, ast.Expr)
        and isinstance(following.value, ast.Constant)
        and isinstance(following.value.value, str)
    )


def _is_overload(function):
    return any(
        isinstance(decorator, ast.Name) and decorator.id == "overload"
        for decorator in function.decorator_list
    )


def _stub_class_problems(class_node):
    """What is wrong with one class of the stub, compared with the module's class."""
    name = class_node.name
    if name not in vars(dexpp):
        return [f"the module has no class {name}"]
    runtime_class = vars(dexpp)[name]
    problems = []
    if ast.get_docstring(class_node) is None:
        problems.append(f"{name} has no docstring")

    stub_members = set()
    for index, item in enumerate(class_node.body):
        if isinstance(item, ast.FunctionDef):
            member, documented = item.name, ast.get_docstring(item) is not None
            if _is_overload(item) is True or (member.startswith("__") and member != "__init__"):
                stub_members.add(member)
                continue
        elif isinstance(item, ast.AnnAssign):
            member, documented = item.target.id, _has_docstring(class_node.body, index)
        elif isinstance(item, ast.Assign):
            member, documented = item.targets[0].id, _has_docstring(class_node.body, index)
        else:
            continue
        stub_members.add(member)
        if not hasattr(runtime_class, member):
            problems.append(f"the module has no {name}.{member}")
        elif isinstance(item, ast.FunctionDef):
            problems.extend(_property_problems(name, item, runtime_class))
        if documented is False:
            problems.append(f"{name}.{member} has no docstring")

    runtime_members = {member for member in vars(runtime_class) if not member.startswith("_")}
    for member in sorted(runtime_members - stub_members):
        problems.append(f"the stub has no {name}.{member}")

    # The stub declares the enums as enum.Enum / enum.IntFlag classes, so the
    # module must not have a pybind11 enum (they keep their members in
    # `__entries`), and each enum must match the stub member by member.
    if "__entries" in vars(runtime_class):
        problems.append(f"{name} is a pybind11 enum, not a Python enum")
    if isinstance(runtime_class, enum.EnumMeta):
        problems.extend(_stub_enum_problems(class_node, runtime_class))
    return problems


def _stub_enum_problems(class_node, runtime_class):
    """What is wrong with one enum of the stub: its base class and its members."""
    name = class_node.name
    problems = []
    stub_bases = [base.id for base in class_node.bases]
    runtime_bases = [base.__name__ for base in runtime_class.__bases__]
    if stub_bases != runtime_bases:
        problems.append(f"{name} derives from {runtime_bases}, the stub says {stub_bases}")
    # Every member with its value, aliases included (AccessFlags.BRIDGE has
    # the value of VOLATILE): `kind.value` is the documented way to get the
    # number, so the stub must give the right one.
    stub_values = {
        item.targets[0].id: ast.literal_eval(item.value)
        for item in class_node.body
        if isinstance(item, ast.Assign)
    }
    runtime_values = {member: value.value for member, value in runtime_class.__members__.items()}
    if stub_values != runtime_values:
        problems.append(f"{name} has members {runtime_values}, the stub says {stub_values}")
    return problems


def _property_problems(class_name, function_node, runtime_class):
    """Whether a member is a property in the stub exactly when it is one in the module.

    The rule of the reference: a property is cheap or cached, a method computes.
    A stub that shows a property where the module has a method (or the other
    way round) would make every call written from the docs fail.
    """
    member = function_node.name
    in_stub = "property" in _decorators(function_node)
    in_module = isinstance(inspect.getattr_static(runtime_class, member), property)
    if in_stub == in_module:
        return []
    kinds = {True: "a property", False: "a method"}
    return [f"{class_name}.{member} is {kinds[in_module]} in the module, {kinds[in_stub]} in the stub"]


def test_work_done_again_on_every_read_is_a_method():
    # The rule of the reference: a property is cheap or cached, a method
    # computes.  Each of these does work in proportion to the whole program,
    # the whole archive, many call sites or a method's bytecode every time, so
    # it must stay a method.
    computed = {
        dexpp.AnalysisContext: ["classes", "strings"],
        dexpp.Xrefs: ["referenced_strings"],
        dexpp.CallNode: ["callers", "callees"],
        dexpp.Method: ["calls", "field_accesses", "string_loads", "type_uses"],
        dexpp.Apk: ["entries"],
        dexpp.Model: ["class_descriptors", "required_version", "pool_stats"],
    }
    for runtime_class, members in computed.items():
        for member in members:
            attribute = inspect.getattr_static(runtime_class, member)
            assert callable(attribute) and not isinstance(attribute, property), member


def test_stub_matches_module_and_is_documented():
    # The Python reference of the documentation is generated from the stub
    # file.  So the stub must name exactly what the module has, and every
    # public name in it must have a docstring.
    with open(STUB_PATH, encoding="utf-8") as f:
        stub = ast.parse(f.read())

    problems = []
    stub_names = set()
    for node in stub.body:
        if isinstance(node, ast.ClassDef):
            stub_names.add(node.name)
            problems.extend(_stub_class_problems(node))
        elif isinstance(node, ast.FunctionDef):
            stub_names.add(node.name)
            if node.name not in vars(dexpp):
                problems.append(f"the module has no function {node.name}")
            if ast.get_docstring(node) is None:
                problems.append(f"{node.name} has no docstring")
        elif isinstance(node, ast.Assign):
            # A type alias, such as `Instruction = Union[...]`.
            alias = node.targets[0].id
            stub_names.add(alias)
            if alias not in vars(dexpp):
                problems.append(f"the module has no {alias}")
            if _has_docstring(stub.body, stub.body.index(node)) is False:
                problems.append(f"{alias} has no docstring")

    module_names = {
        name
        for name, value in vars(dexpp).items()
        if not name.startswith("_") and (isinstance(value, type) or callable(value))
    }
    for name in sorted(module_names - stub_names):
        problems.append(f"the stub has no {name}")

    assert problems == [], "\n".join(problems)


# ===== keyword arguments =====
#
# Every function and method in the stub must accept its arguments under the
# names the stub gives them: a call such as ctx.find_class(descriptor=...)
# passes mypy, so it must work at runtime too.  The test compares, for each
# function, the stub's parameters with the module's: the names in order, the
# kind of each (positional or keyword-only) and whether it has a default.
#
# How the module's parameters are read:
# - A function written in Python (the exception classes) has a real
#   signature, read with inspect.signature.
# - A pybind11 function has no __text_signature__, so inspect.signature fails
#   on it.  pybind11 writes the signature as the first line of __doc__
#   instead, for example
#   "find_class(self: dexpp.AnalysisContext, descriptor: str) -> object".
#   It builds that line from the same argument records it matches keyword
#   arguments against when the function is called: a name given with
#   py::arg is printed as that name, an argument without py::arg is printed
#   as arg0, arg1, ... and cannot be passed by keyword, and py::kw_only() is
#   printed as "*".  So the line shows exactly what a call accepts.  A
#   function whose __doc__ does not start with one plain signature (an
#   overloaded function, or a module built with signatures turned off) is
#   reported as a problem, so the test cannot pass by reading nothing.
#
# Dunder methods other than __init__ are skipped: Python's syntax calls them
# (len(x), x[i], a == b), never with keyword arguments.


def _split_parameters(text):
    """Split a parameter list at the commas outside brackets and quotes."""
    parts, current, depth, quote, escaped = [], [], 0, None, False
    for char in text:
        if quote is not None:
            if escaped is True:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
            continue
        current.append(char)
    if "".join(current).strip() != "":
        parts.append("".join(current).strip())
    return parts


def _pybind11_parameters(function):
    """(name, kind, has_default) of each parameter, from the signature line of
    a pybind11 function's __doc__; None when the doc has no plain signature."""
    doc = function.__doc__ or ""
    head = function.__name__ + "("
    if doc.startswith(head) is False or "\nOverloaded function." in doc:
        return None
    depth, end = 0, None
    for index in range(len(head) - 1, len(doc)):
        if doc[index] == "(":
            depth += 1
        elif doc[index] == ")":
            depth -= 1
            if depth == 0:
                end = index
                break
    parameters, kind = [], "positional"
    for part in _split_parameters(doc[len(head) : end]):
        if part == "*":
            kind = "keyword"
        elif part == "/":
            parameters = [(name, "positional-only", default) for name, _, default in parameters]
        elif part.startswith("*"):
            parameters.append((part.split(":")[0], "variadic", False))
        else:
            parameters.append((part.split(":")[0].strip(), kind, " = " in part))
    return parameters


_KINDS = {
    inspect.Parameter.POSITIONAL_ONLY: "positional-only",
    inspect.Parameter.POSITIONAL_OR_KEYWORD: "positional",
    inspect.Parameter.KEYWORD_ONLY: "keyword",
    inspect.Parameter.VAR_POSITIONAL: "variadic",
    inspect.Parameter.VAR_KEYWORD: "variadic",
}


def _runtime_parameters(function):
    """(name, kind, has_default) of each parameter of a module function."""
    if isinstance(function, types.FunctionType):
        return [
            (p.name, _KINDS[p.kind], p.default is not inspect.Parameter.empty)
            for p in inspect.signature(function).parameters.values()
        ]
    return _pybind11_parameters(function)


def _stub_parameters(function_node):
    """(name, kind, has_default) of each parameter of a stub function."""
    arguments = function_node.args
    positional = arguments.posonlyargs + arguments.args
    first_default = len(positional) - len(arguments.defaults)
    parameters = []
    for index, argument in enumerate(positional):
        kind = "positional-only" if index < len(arguments.posonlyargs) else "positional"
        parameters.append((argument.arg, kind, index >= first_default))
    if arguments.vararg is not None:
        parameters.append(("*" + arguments.vararg.arg, "variadic", False))
    for argument, default in zip(arguments.kwonlyargs, arguments.kw_defaults):
        parameters.append((argument.arg, "keyword", default is not None))
    if arguments.kwarg is not None:
        parameters.append(("**" + arguments.kwarg.arg, "variadic", False))
    return parameters


def _decorators(function_node):
    return {d.id for d in function_node.decorator_list if isinstance(d, ast.Name)}


def _parameter_problems(label, function_node, runtime_function, is_method):
    """What differs between the stub's and the module's parameters of one function."""
    expected = _stub_parameters(function_node)
    actual = _runtime_parameters(runtime_function)
    if actual is None:
        return [f"{label}: the module's signature cannot be read from its __doc__"]
    if is_method is True:
        # Both name the instance `self`; it is never passed by keyword.
        expected, actual = expected[1:], actual[1:]
    if actual != expected:
        return [f"{label}: the module has {actual}, the stub says {expected}"]
    return []


def _keyword_problems(stub):
    """Every stub function whose parameters the module does not accept by name."""
    problems = []
    for node in stub.body:
        if isinstance(node, ast.FunctionDef):
            problems.extend(_parameter_problems(node.name, node, vars(dexpp)[node.name], False))
        if isinstance(node, ast.ClassDef) is False:
            continue
        runtime_class = vars(dexpp)[node.name]
        for item in node.body:
            if isinstance(item, ast.FunctionDef) is False:
                continue
            decorators = _decorators(item)
            dunder = item.name.startswith("__") and item.name != "__init__"
            if dunder is True or decorators & {"property", "overload"}:
                continue
            problems.extend(
                _parameter_problems(
                    f"{node.name}.{item.name}",
                    item,
                    getattr(runtime_class, item.name),
                    "staticmethod" not in decorators,
                )
            )
    return problems


def test_stub_parameters_can_be_passed_by_keyword():
    with open(STUB_PATH, encoding="utf-8") as f:
        stub = ast.parse(f.read())
    problems = _keyword_problems(stub)
    assert problems == [], "\n".join(problems)


def test_keyword_arguments_work():
    # A few real calls by keyword, one of each kind of function the test
    # above reads: a pybind11 method, a pybind11 static method, a keyword-only
    # argument, and a class written in Python.
    ctx = dexpp.AnalysisContext.from_apk(path="tests/data/app.apk", threads=1)
    assert ctx.find_class(descriptor="LTestDex;").descriptor == "LTestDex;"
    sites = ctx.xrefs.string_refs(value="Hello Dex++!!")
    assert len(sites) == 1
    assert ctx.xrefs.referrer_id(referrer=sites[0].referrer).name == "helloDex"
    assert ctx.class_hierarchy.is_subtype_of(sub="LTestDex;", super="Ljava/lang/Object;")
    declared = ctx.call_graph.edges.where(origin=dexpp.EdgeOrigin.DECLARED)
    assert len(declared) > 0

    apk = dexpp.Apk.open(path="tests/data/sample.apk")
    app_name = apk.resources.id_of(type="string", entry="app_name")
    assert apk.resolve_string(reference_id=app_name) == "Dexpp Sample"
    assert len(apk.manifest.components_of(kind=dexpp.ComponentKind.ACTIVITY)) == 1

    with open("tests/data/shapes.dex", "rb") as f:
        model = dexpp.Model.read(data=f.read(), debug_info=False)
    assert model.has_class(descriptor="LCircle;")
    error = dexpp.InvalidModelError(message="broken", problems=[])
    assert str(error) == "broken" and error.problems == []


def test_access_flags():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    flags = cls.access_flags
    assert isinstance(flags, dexpp.AccessFlags)
    assert isinstance(flags, enum.IntFlag) and isinstance(flags, int)
    # Still the number it was, so code that tests bits by value keeps working.
    assert flags == 0x1 and flags & 0x1
    assert dexpp.AccessFlags.PUBLIC in flags
    assert dexpp.AccessFlags.FINAL not in flags

    methods = {m.name: m for m in cls.methods}
    assert dexpp.AccessFlags.CONSTRUCTOR in methods["<init>"].access_flags
    hello = methods["helloDex"].access_flags
    assert hello == dexpp.AccessFlags.PUBLIC | dexpp.AccessFlags.STATIC
    assert methods["helloDex"].is_static and dexpp.AccessFlags.STATIC in hello

    field = next(iter(cls.fields), None)
    if field is not None:
        assert isinstance(field.access_flags, dexpp.AccessFlags)

    info = dexpp.Model.read_file("tests/data/shapes.dex").class_info("LShape;")
    assert isinstance(info.access_flags, dexpp.AccessFlags)
    assert dexpp.AccessFlags.INTERFACE in info.access_flags

    # The DEX format gives two pairs of flags the same bit.
    assert dexpp.AccessFlags.BRIDGE is dexpp.AccessFlags.VOLATILE
    assert dexpp.AccessFlags.VARARGS is dexpp.AccessFlags.TRANSIENT


# The enums and their members, in value order: member i has the value i.
ENUMS = {
    dexpp.InvokeKind: [
        "VIRTUAL", "SUPER", "DIRECT", "STATIC", "INTERFACE", "POLYMORPHIC", "CUSTOM",
    ],
    dexpp.EdgeOrigin: ["DECLARED", "INHERITED_RESOLUTION"],
    dexpp.EdgeKind: [
        "FALLTHROUGH", "GOTO", "BRANCH_TAKEN", "BRANCH_NOT_TAKEN", "SWITCH_CASE",
        "SWITCH_DEFAULT", "EXCEPTION",
    ],
    dexpp.TypeRefKind: [
        "CONST_CLASS", "CHECK_CAST", "INSTANCE_OF", "NEW_INSTANCE", "NEW_ARRAY",
        "FILLED_NEW_ARRAY",
    ],
    dexpp.OperandWidth: ["SINGLE", "PAIR"],
    dexpp.AnnotationVisibility: ["BUILD", "RUNTIME", "SYSTEM"],
    dexpp.ComponentKind: ["ACTIVITY", "SERVICE", "RECEIVER", "PROVIDER"],
}


def test_enums_are_python_enums():
    for cls, names in ENUMS.items():
        assert issubclass(cls, enum.Enum) and not issubclass(cls, int), cls
        assert cls.__module__ == "dexpp"
        assert [member.name for member in cls] == names
        assert [member.value for member in cls] == list(range(len(names)))
        for member in cls:
            assert isinstance(member, enum.Enum)
            assert cls(member.value) is member and cls[member.name] is member
            assert pickle.loads(pickle.dumps(member)) is member
            # A plain Enum is not a number: `kind.value` is the number.
            try:
                int(member)
                raise AssertionError("expected TypeError")
            except TypeError:
                pass


def _raises_type_error(action):
    try:
        action()
    except TypeError:
        return True
    return False


def test_enums_cross_the_boundary_as_members():
    # C++ to Python: every field of an enum type gives a member of the class.
    pets = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    edges = pets.call_graph.edges
    assert {type(e.kind) for e in edges} == {dexpp.InvokeKind}
    assert {type(e.origin) for e in edges} == {dexpp.EdgeOrigin}
    assert dexpp.InvokeKind.INTERFACE in {e.kind for e in edges}
    builder = pets.xrefs.type_refs("Ljava/lang/StringBuilder;")
    assert builder[0].kind is dexpp.TypeRefKind.NEW_INSTANCE
    noise = next(m for m in pets.find_class("LKennel;").methods if m.name == "noise")
    assert noise.calls()[0].kind is dexpp.InvokeKind.INTERFACE

    fibonacci = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
    methods = {m.name: m for m in fibonacci.find_class("LFibonacci;").methods}
    kinds = {e.kind for b in methods["compute"].cfg.blocks for e in b.successors}
    assert {type(k) for k in kinds} == {dexpp.EdgeKind}
    assert dexpp.EdgeKind.BRANCH_TAKEN in kinds
    widths = {getattr(i, "width", None) for i in methods["classify"].instructions}
    assert dexpp.OperandWidth.PAIR in widths

    annotated = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
    visibilities = {a.visibility for a in annotated.find_class("LAnnotated;").annotations}
    assert dexpp.AnnotationVisibility.RUNTIME in visibilities

    # Python to C++: the arguments of an enum type take a member.
    manifest = dexpp.Apk.open("tests/data/sample.apk").manifest
    services = manifest.components_of(dexpp.ComponentKind.SERVICE)
    assert [c.kind for c in services] == [dexpp.ComponentKind.SERVICE]
    interface_calls = edges.where(origin=dexpp.EdgeOrigin.DECLARED, kind=dexpp.InvokeKind.INTERFACE)
    assert len(interface_calls) > 0
    assert all(e.kind is dexpp.InvokeKind.INTERFACE for e in interface_calls)

    # Anything else is a TypeError: a number, a name, a member of another enum.
    for action in (
        lambda: edges.where(origin=0),
        lambda: edges.where(kind="VIRTUAL"),
        lambda: edges.where(origin=dexpp.InvokeKind.VIRTUAL),
        lambda: manifest.components_of(0),
        lambda: manifest.components_of(dexpp.EdgeKind.GOTO),
        lambda: manifest.components_of(None),
    ):
        assert _raises_type_error(action)


def test_type_aliases_exist_at_runtime():
    # The stub's two unions are real objects, usable outside annotations.
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    hello = next(m for m in cls.methods if m.name == "helloDex")
    assert all(isinstance(i, dexpp.Instruction) for i in hello.instructions)
    assert not isinstance("const-string", dexpp.Instruction)

    annotated = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex").find_class("LAnnotated;")
    for field in annotated.fields:
        assert isinstance(field.initial_value, dexpp.AnnotationValue)
    assert not isinstance(object(), dexpp.AnnotationValue)


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"PASS {t.__name__}")
    print(f"{len(tests)} tests passed")


if __name__ == "__main__":
    main()
