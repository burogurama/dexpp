"""Smoke tests for the dexpp Python bindings.

Run from the project root (fixtures are referenced relatively):

    PYTHONPATH=build python3 bindings/python/test_dexpp.py

Also runnable under pytest with the same PYTHONPATH.
"""

import ast
import enum
import gc
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
    assert cls.package == ""  # the default package
    assert ctx.find_class("LNoSuch;") is None


def test_class_package_uses_dots_like_the_manifest():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/packaged.dex")
    cls = ctx.find_class("Lcom/example/app/Packaged;")
    inner = ctx.find_class("Lcom/example/app/Packaged$Inner;")
    assert cls is not None and inner is not None
    assert cls.package == "com.example.app"
    assert inner.package == "com.example.app"


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


def test_method_and_field_ids():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    main = next(m for m in ctx.find_class("LTestDex;").methods if m.name == "main")
    assert main.proto == "([Ljava/lang/String;)V"
    assert main.id == dexpp.MethodId("LTestDex;", "main", "([Ljava/lang/String;)V")
    # The id finds the method's node in the call graph.
    assert ctx.call_graph.find(main.id).method == main

    annotated = dexpp.AnalysisContext.from_dex("tests/data/annotated.dex")
    magic = next(f for f in annotated.find_class("LAnnotated;").fields if f.name == "MAGIC")
    assert magic.id == dexpp.FieldId("LAnnotated;", "MAGIC", "I")


def test_instruction_shapes():
    # handles.dex, Handles.run: invoke-polymorphic {v4, v5}; move-result-object v0;
    # array-length v1, v5; const-method-handle v2; const-method-type v3;
    # invoke-custom {v1}; return-object v0.  Handles.spread: invoke-polymorphic/range.
    handles = dexpp.AnalysisContext.from_dex("tests/data/handles.dex").find_class("LHandles;")
    methods = {m.name: m for m in handles.methods}
    run = methods["run"].instructions
    assert [type(i).__name__ for i in run] == [
        "InvokePolymorphicInstruction", "MoveInstruction", "ArrayLengthInstruction",
        "ConstMethodHandleInstruction", "ConstMethodTypeInstruction",
        "InvokeCustomInstruction", "ReturnInstruction",
    ]
    poly = run[0]
    assert poly.arg_regs == [4, 5]
    assert dexpp.to_string(poly) == (
        f"invoke-polymorphic method@{poly.method_index} proto@{poly.proto_index} {{v4, v5}}"
    )
    assert run[1].src is None and run[1].dest == 0
    assert (run[2].dest, run[2].array_reg) == (1, 5)
    assert dexpp.to_string(run[2]) == "array-length v1, v5"
    assert run[3].dest == 2
    assert dexpp.to_string(run[3]) == f"const-method-handle v2, method_handle@{run[3].method_handle_index}"
    assert run[4].dest == 3
    assert dexpp.to_string(run[4]) == f"const-method-type v3, proto@{run[4].proto_index}"
    spread = methods["spread"].instructions[0]
    assert isinstance(spread, dexpp.InvokePolymorphicInstruction)
    assert spread.arg_regs == [0, 1, 2, 3, 4, 5] and spread.proto_index != poly.proto_index

    # The call is a forward reference with the polymorphic kind.
    calls = methods["run"].calls()
    assert [(c.target.name, c.kind) for c in calls] == [
        ("invokeExact", dexpp.InvokeKind.POLYMORPHIC)
    ]

    # A plain move has a source register; move-exception has none.
    trycatch = dexpp.AnalysisContext.from_dex("tests/data/trycatch.dex").find_class("LTryCatch;")
    safe = next(m for m in trycatch.methods if m.name == "safeParse")
    moves = [i for i in safe.instructions if isinstance(i, dexpp.MoveInstruction)]
    exception = [i for i in moves if dexpp.opcode_name(i.base.opcode) == "move-exception"]
    assert exception and all(i.src is None for i in exception)
    assert all(isinstance(i.src, int) for i in moves
               if not dexpp.opcode_name(i.base.opcode).startswith(("move-result", "move-exception")))


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
    assert dexpp.CfgEdgeKind.BRANCH_TAKEN in kinds or dexpp.CfgEdgeKind.GOTO in kinds
    assert all(e.case_key is None and e.catch_type is None
               for b in cfg.blocks for e in b.successors)


def test_cfg_switch_and_exception_edges():
    switches = dexpp.AnalysisContext.from_dex("tests/data/switches.dex")
    packed = next(m for m in switches.find_class("LSwitches;").methods if m.name == "packed")
    entry = packed.cfg.entry
    cases = [e for e in entry.successors if e.kind is dexpp.CfgEdgeKind.SWITCH_CASE]
    assert [e.case_key for e in cases] == [10, 11, 12, 13]
    default = [e for e in entry.successors if e.kind is dexpp.CfgEdgeKind.SWITCH_DEFAULT]
    assert len(default) == 1 and default[0].case_key is None

    trycatch = dexpp.AnalysisContext.from_dex("tests/data/trycatch.dex").find_class("LTryCatch;")
    methods = {m.name: m for m in trycatch.methods}
    edges = [e for b in methods["divide"].cfg.blocks for e in b.successors]
    typed = [e for e in edges if e.kind is dexpp.CfgEdgeKind.EXCEPTION]
    assert typed and all(e.catch_type == "Ljava/lang/ArithmeticException;" for e in typed)
    assert all(e.catch_type is None for e in edges if e.kind is not dexpp.CfgEdgeKind.EXCEPTION)
    safe = methods["safeParse"]
    catch_all = [
        e for b in safe.cfg.blocks for e in b.successors
        if e.kind is dexpp.CfgEdgeKind.EXCEPTION_CATCH_ALL
    ]
    assert catch_all and all(e.catch_type is None for e in catch_all)
    handlers = {t.catch_all_offset for t in safe.try_blocks}
    assert {safe.cfg.blocks[e.target_block].start_offset for e in catch_all} <= handlers


def test_call_graph():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
    g = ctx.call_graph
    assert not g.empty

    sound = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")
    node = g.find(sound)
    assert node is not None and node.kind is dexpp.CallNodeKind.DEFINED
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
    assert g.nodes[inherited[0].callee_node].kind is dexpp.CallNodeKind.DEFINED

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


def test_call_graph_node_kinds():
    # handles.dex: Handles.run makes an invoke-polymorphic and an invoke-custom call.
    g = dexpp.AnalysisContext.from_dex("tests/data/handles.dex").call_graph
    run = next(n for n in g.nodes if n.id.name == "run")
    assert run.kind is dexpp.CallNodeKind.DEFINED and run.method is not None
    polymorphic, custom = run.out_edges
    assert polymorphic.kind is dexpp.InvokeKind.POLYMORPHIC
    assert polymorphic.callee.kind is dexpp.CallNodeKind.EXTERNAL
    assert polymorphic.callee.id.name == "invokeExact"
    assert custom.kind is dexpp.InvokeKind.CUSTOM
    assert custom.callee.kind is dexpp.CallNodeKind.CALL_SITE
    assert custom.callee.method is None and g.method_for(custom.callee) is None
    assert [n.kind for n in g.nodes].count(dexpp.CallNodeKind.CALL_SITE) == 1


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
    external = next(n for n in nodes if n.kind is dexpp.CallNodeKind.EXTERNAL)
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
            [(n.id.class_descriptor, n.id.name, n.id.proto, n.kind) for n in g.nodes],
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


def test_disassemble():
    ctx = dexpp.AnalysisContext.from_dex("tests/data/classes.dex")
    cls = ctx.find_class("LTestDex;")
    main = next(m for m in cls.methods if m.name == "main")
    lines = main.disassemble()
    assert [(line.offset, line.text) for line in lines] == [
        (0, "sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;"),
        (2, "invoke-static {}, LTestDex;->helloDex()Ljava/lang/String;"),
        (5, "move-result-object v0"),
        (6, "invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V"),
        (9, "return-void"),
    ]
    # One line per instruction, at the instruction's offset.
    assert [line.offset for line in lines] == [i.base.offset for i in main.instructions]
    # A method makes a new list on each call.
    assert main.disassemble() is not lines

    # Strings are quoted, with what would hide or break the line escaped.
    clinit = _method("tests/data/multistring.dex", "LMultiStringTest;", "<clinit>")
    texts = [line.text for line in clinit.disassemble()]
    assert any(t.endswith(' "test\\u0000embedded"') for t in texts), texts
    assert any(t.endswith(' ""') for t in texts), texts

    # Method handles, method types, call sites and a /range form.
    run = _method("tests/data/handles.dex", "LHandles;", "run")
    texts = [line.text for line in run.disassemble()]
    assert "const-method-handle v2, invoke-static@LHandles;->target(I)I" in texts
    assert "const-method-type v3, (I)I" in texts
    assert any(t.startswith('invoke-custom {v1}, call_site@0("apply", (I)V)@LHandles;->bootstrap(')
               for t in texts), texts
    spread = _method("tests/data/handles.dex", "LHandles;", "spread")
    assert spread.disassemble()[0].text.startswith("invoke-polymorphic/range {v0 .. v5}, ")

    # A switch table is written out; its targets are the offsets of lines.
    packed = _method("tests/data/switches.dex", "LSwitches;", "packed")
    lines = packed.disassemble()
    assert lines[0].text == "packed-switch v0, {0xa -> 000e, 0xb -> 000b, 0xc -> 0008, 0xd -> 0005}"
    assert {0x0E, 0x0B, 0x08, 0x05} <= {line.offset for line in lines}

    # No code, no lines.
    assert _method("tests/data/shapes.dex", "LShape;", "area").disassemble() == []


def test_disassembly_lines_own_their_text():
    def take():
        return _method("tests/data/classes.dex", "LTestDex;", "helloDex").disassemble()

    lines, busy = _after_owners_are_freed(take)
    assert [(line.offset, line.text) for line in lines] == [
        (0, 'const-string v0, "Hello Dex++!!"'),
        (2, "return-object v0"),
    ]


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
    assert ("Ljava/lang/StringBuilder;", dexpp.TypeUseKind.NEW_INSTANCE) in [
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


def test_method_handle_and_method_type_values():
    # handles.dex: @HandleMarker(getter = <static-get Handles.counter>,
    # invoker = <invoke-static Handles.target(I)I>, type = <method type (I)I>)
    handles = dexpp.AnalysisContext.from_dex("tests/data/handles.dex").find_class("LHandles;")
    marker = next(a for a in handles.annotations if a.type_descriptor == "LHandleMarker;")
    getter = marker.elements["getter"]
    assert isinstance(getter, dexpp.MethodHandleValue)
    assert getter.kind is dexpp.MethodHandleKind.STATIC_GET
    assert getter.member == dexpp.FieldId("LHandles;", "counter", "I")
    invoker = marker.elements["invoker"]
    assert isinstance(invoker, dexpp.MethodHandleValue)
    assert invoker.kind is dexpp.MethodHandleKind.INVOKE_STATIC
    assert invoker.member == dexpp.MethodId("LHandles;", "target", "(I)I")
    method_type = marker.elements["type"]
    assert isinstance(method_type, dexpp.MethodTypeValue) and method_type.proto == "(I)I"
    assert isinstance(getter, dexpp.AnnotationValue)


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
    # Exported and guarded by its own normal / dangerous / undeclared-platform
    # permission -> reachable, although the application requires a signature one.
    assert by_name["NormalActivity"].is_reachable
    assert not by_name["NormalActivity"].permission_inherited
    assert by_name["DangerousActivity"].is_reachable
    assert by_name["PlatformPermActivity"].is_reachable
    assert by_name["NormalReceiver"].is_reachable
    # Exported but guarded by an app-declared signature permission -> not reachable.
    assert by_name["SigActivity"].is_exported
    assert by_name["SigActivity"].guarded_by_signature_permission
    assert not by_name["SigActivity"].is_reachable
    assert by_name["SigService"].is_exported and not by_name["SigService"].is_reachable
    # Not exported -> not reachable.
    assert not by_name["HiddenActivity"].is_exported
    assert not by_name["HiddenActivity"].is_reachable

    # Two of the eight activities are aliases.
    activities = m.components_of(dexpp.ComponentKind.ACTIVITY)
    assert sum(c.is_exported for c in activities) == 7
    assert sum(c.is_reachable for c in activities) == 4


def test_activity_aliases():
    # An <activity-alias> is an activity with a target. It takes only its own
    # permission: not its target's, not the application's.
    m = dexpp.Apk.open("tests/data/reachable.apk").manifest
    assert m is not None
    by_name = {c.name.split(".")[-1]: c for c in m.components}

    open_alias = by_name["OpenAlias"]
    assert open_alias.kind == dexpp.ComponentKind.ACTIVITY
    assert open_alias.target_activity == "com.dexpp.reach.SigActivity"
    assert open_alias.permission is None
    assert not open_alias.permission_inherited
    assert open_alias.is_reachable

    guarded = by_name["GuardedAlias"]
    assert guarded.target_activity == "com.dexpp.reach.InheritingActivity"
    assert guarded.permission == "com.dexpp.reach.P_SIGNATURE"
    assert guarded.guarded_by_signature_permission and not guarded.is_reachable

    assert by_name["SigActivity"].target_activity is None
    names = [c.name.split(".")[-1] for c in m.components_of(dexpp.ComponentKind.ACTIVITY)]
    assert names[-2:] == ["OpenAlias", "GuardedAlias"]


def test_path_permissions():
    m = dexpp.Apk.open("tests/data/reachable.apk").manifest
    assert m is not None
    by_name = {c.name.split(".")[-1]: c for c in m.components}

    # /public is readable with a normal permission: the provider is reachable,
    # although its own permissions are signature ones.
    path = by_name["PathProvider"]
    assert len(path.path_permissions) == 1
    pp = path.path_permissions[0]
    assert isinstance(pp, dexpp.PathPermission)
    assert pp.path == "/public"
    assert pp.match == dexpp.PathMatch.PREFIX
    assert not pp.path_unknown
    assert pp.read_permission == "com.dexpp.reach.P_NORMAL"
    assert not pp.read_permission_unknown
    assert pp.write_permission is None and not pp.write_permission_unknown
    assert path.read_permission == "com.dexpp.reach.P_SIGNATURE"
    assert not path.guarded_by_signature_permission and path.is_reachable
    assert repr(pp) == "PathPermission(/public)"

    # Two of its three elements are dropped, as Android drops them.
    sig = by_name["SigPathProvider"]
    assert [(p.path, p.match) for p in sig.path_permissions] == [
        ("/items/.*", dexpp.PathMatch.PATTERN)
    ]
    assert sig.guarded_by_signature_permission and not sig.is_reachable

    assert by_name["SigActivity"].path_permissions == []
    assert [k.name for k in dexpp.PathMatch] == [
        "LITERAL", "PREFIX", "PATTERN", "SUFFIX", "ADVANCED_PATTERN"
    ]


def test_provider_exported_default():
    # reachable.apk targets API 34: a provider without android:exported is
    # not exported, whatever its intent filters.
    m = dexpp.Apk.open("tests/data/reachable.apk").manifest
    assert m is not None
    by_name = {c.name.split(".")[-1]: c for c in m.components}
    provider = by_name["DefaultProvider"]
    assert provider.intent_filters
    assert provider.exported is None
    assert provider.exported_default is False
    assert not provider.is_exported
    # The other kinds: true when there is an intent filter.
    assert by_name["HiddenActivity"].exported_default is False
    sample = dexpp.Apk.open("tests/data/sample.apk").manifest
    assert sample is not None
    launcher = sample.components_of(dexpp.ComponentKind.ACTIVITY)[0]
    assert launcher.exported_default is True


def test_application_permission():
    # reachable.apk's <application> requires its signature permission, and the
    # components without a permission of their own take it.
    m = dexpp.Apk.open("tests/data/reachable.apk").manifest
    assert m is not None
    assert m.application_permission == "com.dexpp.reach.P_SIGNATURE"
    assert m.application_permission_reference is None
    assert not m.application_permission_unknown

    by_name = {c.name.split(".")[-1]: c for c in m.components}
    for name in ["InheritingActivity", "InheritingService", "InheritingReceiver"]:
        c = by_name[name]
        assert c.permission == "com.dexpp.reach.P_SIGNATURE", name
        assert c.permission_inherited and not c.permission_unknown, name
        assert c.permission_reference is None, name
        assert c.is_exported and not c.is_reachable, name
        assert c.read_permission is None and c.write_permission is None, name

    # A provider is guarded only when reading and writing both are.
    inheriting = by_name["InheritingProvider"]
    assert inheriting.read_permission == "com.dexpp.reach.P_SIGNATURE"
    assert inheriting.write_permission == "com.dexpp.reach.P_SIGNATURE"
    assert inheriting.read_permission_reference is None
    assert not inheriting.read_permission_unknown and not inheriting.write_permission_unknown
    assert not inheriting.is_reachable
    readable = by_name["ReadableProvider"]
    assert readable.read_permission == "com.dexpp.reach.P_NORMAL"
    assert readable.write_permission == "com.dexpp.reach.P_SIGNATURE"
    assert readable.is_reachable
    writable = by_name["WritableProvider"]
    assert writable.permission == "com.dexpp.reach.P_SIGNATURE"
    assert not writable.permission_inherited
    assert writable.write_permission == "com.dexpp.reach.P_DANGEROUS"
    assert writable.write_permission_reference is None
    assert writable.is_reachable

    sample = dexpp.Apk.open("tests/data/sample.apk").manifest
    assert sample is not None
    assert sample.application_permission is None
    assert not any(c.permission_inherited for c in sample.components)


def test_manifest_null_values():
    # refs.apk: @bool/nothing is @null, @bool/same_as_nothing refers to it,
    # @bool/empty_bool is @empty and @string/no_permission is @null.  Android
    # reads them as not written.
    apk = dexpp.Apk.open("tests/data/refs.apk")
    rt = apk.resources
    m = apk.manifest
    assert rt is not None and m is not None
    by_name = {c.name.split(".")[-1]: c for c in m.components}

    null = by_name["NullActivity"]
    assert null.exported is None
    assert null.exported_reference == rt.id_of("bool", "nothing")
    assert not null.exported_unknown
    assert not null.is_exported  # no intent filter
    chained = by_name["NullFilteredActivity"]
    assert chained.exported is None and not chained.exported_unknown
    assert chained.is_exported  # an intent filter
    assert not by_name["EmptyActivity"].is_exported
    # android:exported="@null" written directly: no reference.
    literal = by_name["LiteralNullActivity"]
    assert literal.exported is None and literal.exported_reference is None
    assert literal.is_exported

    service = by_name["NullPermissionService"]
    assert service.permission is None
    assert service.permission_reference == rt.id_of("string", "no_permission")
    assert not service.permission_unknown and not service.permission_inherited
    assert service.is_reachable

    # ?android:attr/windowFullscreen: a theme attribute, unknown.
    theme = by_name["ThemeActivity"]
    assert theme.exported is None and theme.exported_unknown and theme.is_exported
    # A string that differs between Android versions depends on the device.
    assert by_name["PermissionOnV31Service"].permission_unknown
    assert not by_name["LocalizedPermissionService"].permission_unknown

    assert not m.debuggable_unknown and not m.allow_backup_unknown
    assert not m.min_sdk_unknown and not m.target_sdk_unknown
    assert not m.version_code_unknown and not m.version_name_unknown
    assert not by_name["ExportedActivity"].name_unknown


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


def test_manifest_references():
    # refs.apk writes its boolean and integer attributes as resource references
    # (tests/data/RefsManifest.xml).
    apk = dexpp.Apk.open("tests/data/refs.apk")
    rt = apk.resources
    assert rt is not None
    m = apk.manifest
    assert m is not None

    assert m.min_sdk == 23 and m.min_sdk_reference == rt.id_of("integer", "min_sdk")
    assert m.target_sdk == 34 and m.target_sdk_reference is not None
    assert m.debuggable is True and m.debuggable_reference == rt.id_of("bool", "debug")
    assert m.allow_backup is False and m.allow_backup_reference is not None
    # A plain value has no reference.
    assert m.version_code == 42 and m.version_code_reference is None

    by_name = {c.name.split(".")[-1]: c for c in m.components}
    exported = by_name["ExportedActivity"]
    assert exported.exported is True
    assert exported.exported_reference == rt.id_of("bool", "yes")
    assert exported.is_exported
    hidden = by_name["HiddenActivity"]
    assert hidden.exported is False and not hidden.is_exported
    # A reference to the string "true": Android reads it as true.
    text = by_name["TextActivity"]
    assert text.exported is True
    assert text.exported_reference == rt.id_of("string", "not_a_bool")
    assert text.is_exported and text.is_reachable

    assert m.declared_permission_protection_level("com.dexpp.refs.P_SIGNATURE") == 2
    assert m.declared_permission_protection_level("com.dexpp.refs.P_TEXT") is None
    assert not by_name["GuardedService"].is_reachable
    assert by_name["TextGuardedService"].is_reachable

    # sample.apk writes plain values: no references.
    sample = dexpp.Apk.open("tests/data/sample.apk").manifest
    assert sample is not None
    assert sample.min_sdk_reference is None and sample.debuggable_reference is None
    assert sample.version_name_reference is None
    assert not sample.debuggable_varies and not sample.allow_backup_varies
    for c in sample.components:
        assert c.exported_reference is None and not c.exported_varies
        assert c.name_reference is None and c.permission_reference is None


def test_manifest_values_that_vary_between_configurations():
    # refs.apk: res/values and res/values-v31 give some resources different values.
    apk = dexpp.Apk.open("tests/data/refs.apk")
    rt = apk.resources
    m = apk.manifest
    assert rt is not None and m is not None

    # true by default, false on v31; false by default, true on v31.
    assert m.debuggable is True and m.debuggable_varies
    assert m.allow_backup is False and m.allow_backup_varies

    by_name = {c.name.split(".")[-1]: c for c in m.components}
    variant = by_name["VariantActivity"]
    assert variant.exported is False
    assert variant.exported_varies
    assert variant.is_exported and variant.is_reachable
    assert not by_name["ExportedActivity"].exported_varies

    # Strings read as booleans like Android: only "1", "true" and "TRUE" are true.
    assert by_name["UpperTextActivity"].exported is True
    assert by_name["CapitalTextActivity"].exported is False
    assert not by_name["EmptyTextActivity"].is_exported

    # A protection level that is normal on v31 does not guard.
    assert m.declared_permission_protection_level("com.dexpp.refs.P_VARYING") == 2
    assert m.declared_permission_protection_level_varies(name="com.dexpp.refs.P_VARYING")
    assert not m.declared_permission_protection_level_varies("com.dexpp.refs.P_SIGNATURE")
    assert by_name["VaryingLevelService"].is_reachable


def test_manifest_text_references():
    apk = dexpp.Apk.open("tests/data/refs.apk")
    rt = apk.resources
    m = apk.manifest
    assert rt is not None and m is not None
    assert m.version_name == "1.0"
    assert m.version_name_reference == rt.id_of("string", "version_name")
    assert "com.dexpp.refs.P_BY_REFERENCE" in m.declared_permissions

    by_name = {c.name.split(".")[-1]: c for c in m.components}
    by_reference = by_name["PermissionByReferenceService"]
    assert by_reference.permission == "com.dexpp.refs.P_SIGNATURE"
    assert by_reference.permission_reference == rt.id_of("string", "signature_permission")
    assert by_reference.guarded_by_signature_permission
    assert not by_reference.is_reachable
    # A string with a res/values-fr variant: Android ignores it, so nothing guards.
    localized = by_name["LocalizedPermissionService"]
    assert localized.permission is None
    assert localized.permission_reference == rt.id_of("string", "localized_permission")
    assert localized.is_reachable
    assert not by_name["DeclaredByReferenceService"].is_reachable
    assert by_name["ExportedActivity"].name_reference is None


def test_resource_values():
    rt = dexpp.Apk.open("tests/data/refs.apk").resources
    assert rt is not None
    differs = rt.id_of("bool", "differs_on_v31")
    assert differs is not None
    values = rt.values(differs)
    assert [v.configuration for v in values] == ["", "v31"]
    assert [v.as_bool() for v in values] == [False, True]
    assert values[0].type == 0x12 and values[0].data == 0
    assert values[1].as_int() == -1 and values[1].string is None
    assert "v31" in repr(values[1])

    greeting = rt.id_of("string", "greeting")
    assert greeting is not None
    texts = {v.configuration: v.string for v in rt.values(id=greeting)}
    assert texts == {"": "Hello", "fr": "Bonjour", "b+sr+Latn-land-night-v8": "Zdravo"}

    same_as_yes = rt.id_of("bool", "same_as_yes")
    assert same_as_yes is not None
    (reference,) = rt.values(same_as_yes)
    assert reference.type == 0x01 and reference.data == rt.id_of("bool", "yes")
    assert reference.as_bool() is None

    assert rt.values(0x7F999999) == []
    capital = rt.id_of("string", "capital_true")
    empty = rt.id_of("string", "empty_text")
    assert capital is not None and empty is not None
    assert rt.resolve_bool(capital) is False
    assert rt.resolve_bool(empty) is None


def test_resolve_bool_and_int():
    rt = dexpp.Apk.open("tests/data/refs.apk").resources
    assert rt is not None
    yes = rt.id_of("bool", "yes")
    no = rt.id_of("bool", "no")
    same_as_yes = rt.id_of("bool", "same_as_yes")
    min_sdk = rt.id_of("integer", "min_sdk")
    not_a_bool = rt.id_of("string", "not_a_bool")
    assert None not in (yes, no, same_as_yes, min_sdk, not_a_bool)
    assert rt.resolve_bool(yes) is True
    assert rt.resolve_bool(no) is False
    assert rt.resolve_bool(same_as_yes) is True
    assert rt.resolve_int(min_sdk) == 23
    # The string "true" reads as true, as Android reads it.
    assert rt.resolve_bool(not_a_bool) is True
    assert rt.resolve_int(not_a_bool) is None
    assert rt.resolve_bool(0x7F999999) is None
    assert rt.resolve_int(id=0x7F999999) is None


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


def test_apk_from_bytes_matches_open():
    path = "tests/data/signed.apk"
    with open(path, "rb") as f:
        data = f.read()
    from_file = dexpp.Apk.open(path)
    # Every object that gives its bytes through the buffer protocol works.
    for source in (data, bytearray(data), memoryview(data)):
        apk = dexpp.Apk.from_bytes(source)
        assert apk.entries() == from_file.entries()
        assert apk.read("classes.dex") == from_file.read("classes.dex")
        assert apk.read("missing") is None
        assert apk.manifest.package == from_file.manifest.package
        app_name = apk.resources.id_of("string", "app_name")
        assert apk.resolve_string(app_name) == "Dexpp Sample"
        assert apk.signing.is_signed
        assert [c.sha256_hex for c in apk.signing.certificates] == [
            c.sha256_hex for c in from_file.signing.certificates
        ]
        assert apk.analysis().find_class("LTestDex;") is not None


def test_apk_from_bytes_reads_a_strided_view_in_order():
    with open("tests/data/sample.apk", "rb") as f:
        data = f.read()
    # Every byte of the APK appears twice in `doubled`, and the view takes
    # every other one: its bytes are not next to each other in memory.
    doubled = bytes(byte for byte in data for _ in range(2))
    strided = memoryview(doubled)[::2]
    assert strided.c_contiguous is False
    apk = dexpp.Apk.from_bytes(strided)
    assert apk.read("classes.dex") == dexpp.Apk.open("tests/data/sample.apk").read("classes.dex")

    # A reversed view is not a ZIP archive: it is refused, and nothing is
    # read past the end of the buffer.
    error = _error_from(lambda: dexpp.Apk.from_bytes(memoryview(data)[::-1]))
    assert type(error) is dexpp.InvalidApkError


def test_apk_from_bytes_keeps_its_own_copy():
    with open("tests/data/sample.apk", "rb") as f:
        buffer = bytearray(f.read())
    apk = dexpp.Apk.from_bytes(buffer)
    buffer[:] = bytes(len(buffer))
    del buffer
    gc.collect()
    assert apk.manifest.package == "com.example.dexpp"
    assert apk.read("classes.dex") is not None


def test_apk_from_bytes_errors():
    for data in (b"not an apk", b"", bytearray(100)):
        error = _error_from(lambda: dexpp.Apk.from_bytes(data))
        assert type(error) is dexpp.InvalidApkError
        assert str(error).startswith("<apk>: "), str(error)

    # The label starts the message, where open() puts the path.
    error = _error_from(lambda: dexpp.Apk.from_bytes(b"not an apk", label="upload.apk"))
    assert str(error).startswith("upload.apk: "), str(error)

    # The label is also used by the errors of analysis().
    with open("tests/data/nodex.apk", "rb") as f:
        nodex = dexpp.Apk.from_bytes(f.read(), "nodex-upload.apk")
    error = _error_from(nodex.analysis)
    assert type(error) is dexpp.InvalidApkError
    assert str(error).startswith("nodex-upload.apk: "), str(error)
    error = _error_from(dexpp.Apk.open("tests/data/nodex.apk").analysis)
    assert str(error).startswith("tests/data/nodex.apk: "), str(error)

    # Not a bytes-like object at all.
    assert _raises_type_error(lambda: dexpp.Apk.from_bytes("tests/data/sample.apk"))


def test_apk_analysis_threads():
    # multidex.apk has four deflated DEX entries, enough for the parallel path.
    apk = dexpp.Apk.open("tests/data/multidex.apk")
    automatic = apk.analysis()
    first = automatic.classes()[0]
    # The context is kept per threads value: the same value gives the same
    # context, so its classes compare equal.
    assert apk.analysis().classes()[0] == first
    assert apk.analysis(threads=0).classes()[0] == first
    # Another value builds a separate context, with the same content.
    serial = apk.analysis(threads=1)
    assert serial.classes()[0] != first
    assert apk.analysis(threads=1).classes()[0] == serial.classes()[0]
    assert [c.descriptor for c in serial.classes()] == [c.descriptor for c in automatic.classes()]
    assert len(serial.call_graph.nodes) == len(automatic.call_graph.nodes)


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


# ===== nothing outlives the memory it reads =====
#
# A value object (StringLoad, CfgEdge, BasicBlock, ...) holds no handle, so it
# does not keep the analysis alive: it must own a copy of its strings.  Other
# objects (views, the call graph, a manifest) read memory that belongs to the
# analysis or to the APK, so they must keep that owner alive.  Each test below
# takes objects from an analysis, drops every other reference to it, reuses
# the memory it freed, and only then reads the objects.  An object that still
# pointed into the freed memory would read another analysis's strings, or bytes
# that are not even UTF-8.

# Small fixtures that load as DEX files, used to reuse freed memory.
_BUSY_FIXTURES = (
    "tests/data/classes.dex",
    "tests/data/multistring.dex",
    "tests/data/shapes.dex",
    "tests/data/trycatch.dex",
    "tests/data/fibonacci.dex",
    "tests/data/polymorphic.dex",
    "tests/data/annotated.dex",
    "tests/data/inherit.dex",
)


def _after_owners_are_freed(take):
    """Call take(), which loads an analysis or APK and returns objects taken
    from it, and return those objects once nothing else holds the analysis.

    The locals of take(), the only other references, are gone when it
    returns, and gc.collect() frees any cycle.  Then more analyses are loaded
    and every string, try block and CFG of theirs is decoded, so the C++ heap
    hands the freed memory out again.  Those analyses are returned too: the
    caller keeps them alive while it reads, so the reused memory stays in use.
    """
    taken = take()
    gc.collect()
    busy = []
    for _ in range(10):
        for path in _BUSY_FIXTURES:
            ctx = dexpp.AnalysisContext.from_dex(path)
            ctx.strings()
            for cls in ctx.classes():
                for method in cls.methods:
                    method.parameter_registers
                    method.try_blocks
                    method.cfg
            busy.append(ctx)
    return taken, busy


def _method(path, class_descriptor, name):
    """The method called @p name of a class in a newly loaded DEX file."""
    cls = dexpp.AnalysisContext.from_dex(path).find_class(class_descriptor)
    assert cls is not None
    return next(m for m in cls.methods if m.name == name)


def test_string_loads_own_their_values():
    def take():
        clinit = _method("tests/data/multistring.dex", "LMultiStringTest;", "<clinit>")
        return clinit.string_loads()

    loads, busy = _after_owners_are_freed(take)
    assert [s.value for s in loads] == [
        "Hello, World!",
        "Goodbye, World!",
        "How are you?",
        "I am fine, thanks!",
        "\U0001f389 Party time! \U0001f38a",
        "",
        "test\x00embedded",
    ]


def test_type_uses_own_their_descriptors():
    def take():
        return _method("tests/data/polymorphic.dex", "LDog;", "describe").type_uses()

    uses, busy = _after_owners_are_freed(take)
    assert [(u.descriptor, u.kind) for u in uses] == [
        ("Ljava/lang/StringBuilder;", dexpp.TypeUseKind.NEW_INSTANCE)
    ]


def test_parameter_registers_own_their_types():
    def take():
        main = _method("tests/data/classes.dex", "LTestDex;", "main")
        compute = _method("tests/data/fibonacci.dex", "LFibonacci;", "compute")
        return main.parameter_registers + compute.parameter_registers

    registers, busy = _after_owners_are_freed(take)
    assert [(r.reg, r.type, r.is_this, r.is_wide) for r in registers] == [
        (1, "[Ljava/lang/String;", False, False),
        (9, "LFibonacci;", True, False),
        (10, "I", False, False),
    ]


def test_try_blocks_and_catch_handlers_own_their_types():
    def take():
        divide = _method("tests/data/trycatch.dex", "LTryCatch;", "divide")
        safe = _method("tests/data/trycatch.dex", "LTryCatch;", "safeParse")
        tries = divide.try_blocks + safe.try_blocks
        return tries, [h for t in tries for h in t.handlers]

    (tries, handlers), busy = _after_owners_are_freed(take)
    assert [[h.type_descriptor for h in t.handlers] for t in tries] == [
        ["Ljava/lang/ArithmeticException;"],
        ["Ljava/lang/NumberFormatException;"],
        [],
    ]
    assert [h.type_descriptor for h in handlers] == [
        "Ljava/lang/ArithmeticException;",
        "Ljava/lang/NumberFormatException;",
    ]


def test_cfg_edges_own_their_catch_types():
    def take():
        safe = _method("tests/data/trycatch.dex", "LTryCatch;", "safeParse")
        return [e for b in safe.cfg.blocks for e in b.successors]

    edges, busy = _after_owners_are_freed(take)
    assert [e.catch_type for e in edges if e.kind is dexpp.CfgEdgeKind.EXCEPTION] == [
        "Ljava/lang/NumberFormatException;"
    ]
    assert all(e.catch_type is None for e in edges if e.kind is not dexpp.CfgEdgeKind.EXCEPTION)


def test_basic_blocks_own_their_edges():
    def take():
        cfg = _method("tests/data/trycatch.dex", "LTryCatch;", "divide").cfg
        # Every way to get a block: from the view, a slice of it, iterating
        # it, and the two lookups on the Cfg.
        return [cfg.blocks[0], cfg.blocks[0:1][0], next(iter(cfg.blocks)), cfg.entry,
                cfg.block_at_offset(0)]

    blocks, busy = _after_owners_are_freed(take)
    for block in blocks:
        assert block.index == 0 and block.start_offset == 0
        assert [(e.kind, e.catch_type) for e in block.successors] == [
            (dexpp.CfgEdgeKind.FALLTHROUGH, None),
            (dexpp.CfgEdgeKind.EXCEPTION, "Ljava/lang/ArithmeticException;"),
        ]


def test_borrowed_objects_keep_their_owner_alive():
    # These objects read memory that the analysis or the APK owns, without a
    # copy, so each must keep its owner alive by itself.
    sound = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")

    def take():
        divide = _method("tests/data/trycatch.dex", "LTryCatch;", "divide")
        graph = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex").call_graph
        node = graph.find(sound)
        assert node is not None
        xrefs = dexpp.AnalysisContext.from_dex("tests/data/classes.dex").xrefs
        manifest = dexpp.Apk.open("tests/data/sample.apk").manifest
        assert manifest is not None
        resources = dexpp.Apk.open("tests/data/sample.apk").resources
        assert resources is not None
        signing = dexpp.Apk.open("tests/data/signed.apk").signing
        return {
            "cfg": divide.cfg,
            "blocks": _method("tests/data/trycatch.dex", "LTryCatch;", "divide").cfg.blocks,
            "instructions": _method("tests/data/classes.dex", "LTestDex;", "main").instructions,
            "node_id": node.id,
            "in_edges": node.in_edges,
            "dispatch_targets": node.dispatch_targets,
            "edge": node.in_edges[0],
            "referrers": xrefs.referrers,
            "hierarchy": dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
            .class_hierarchy,
            "components": manifest.components,
            "intent_filters": manifest.components_of(dexpp.ComponentKind.ACTIVITY)[0]
            .intent_filters,
            "manifest": dexpp.Apk.open("tests/data/sample.apk").manifest,
            "resources": resources,
            "certificates": signing.certificates,
        }

    t, busy = _after_owners_are_freed(take)
    assert [(e.kind, e.catch_type) for e in t["cfg"].entry.successors][1] == (
        dexpp.CfgEdgeKind.EXCEPTION, "Ljava/lang/ArithmeticException;"
    )
    assert t["blocks"][0].successors[1].catch_type == "Ljava/lang/ArithmeticException;"
    assert dexpp.to_string(t["instructions"][0]).startswith("sget-object")
    assert (t["node_id"].class_descriptor, t["node_id"].name) == ("LAnimal;", "sound")
    assert {e.caller.id.name for e in t["in_edges"]} == {"noise", "describe"}
    assert {n.id.class_descriptor for n in t["dispatch_targets"]} == {"LDog;", "LCat;"}
    assert t["edge"].callee.id == sound
    assert "helloDex" in [r.name for r in t["referrers"]]
    assert set(t["hierarchy"].all_descendants("LAnimal;")) == {"LPet;", "LDog;", "LCat;"}
    assert "com.example.dexpp.MainActivity" in [c.name for c in t["components"]]
    assert t["intent_filters"][0].is_launcher
    assert t["manifest"].package == "com.example.dexpp"
    app_name = t["resources"].id_of("string", "app_name")
    assert app_name is not None and t["resources"].resolve_string(app_name) == "Dexpp Sample"
    assert t["certificates"][0].subject == "C=MA, O=DexppOrg, CN=Dexpp Test"


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
    assert isinstance(target, dexpp.TypeValue)
    assert target == element("greet").elements["target"]
    assert len({target, element("greet").elements["target"]}) == 1

    retention = next(
        a for a in annotated.find_class("LMarker;").annotations
        if a.type_descriptor == "Ljava/lang/annotation/Retention;"
    )
    policy = retention.elements["value"]
    assert isinstance(policy, dexpp.EnumValue) and policy.constant.name == "RUNTIME"
    assert hash(policy) == hash(retention.elements["value"])

    handles = dexpp.AnalysisContext.from_dex("tests/data/handles.dex").find_class("LHandles;")
    def marker_element(name):
        return next(
            a for a in handles.annotations if a.type_descriptor == "LHandleMarker;"
        ).elements[name]
    invoker = marker_element("invoker")
    assert invoker == marker_element("invoker") and invoker != marker_element("getter")
    assert len({invoker, marker_element("invoker"), marker_element("getter")}) == 2
    assert hash(marker_element("type")) == hash(marker_element("type"))

    resources = dexpp.Apk.open("tests/data/sample.apk").resources
    app_name = resources.id_of("string", "app_name")
    assert resources.name_of(app_name) == resources.name_of(app_name)
    assert len({resources.name_of(app_name), resources.name_of(app_name)}) == 1


# ===== editable DEX model (dex::model) =====

# shapes.dex: interface LShape;, abstract LAbstractShape; implementing it, and
# concrete subclasses LCircle;, LRectangle;.


def test_model_read_and_classes():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    assert len(m) == 4
    assert m.class_descriptors() == [
        "LShape;",
        "LAbstractShape;",
        "LCircle;",
        "LRectangle;",
    ]
    assert "LCircle;" in m
    assert "Lno/such/Class;" not in m
    # One way each to count and to test for a class.
    assert not hasattr(m, "class_count") and not hasattr(m, "has_class")


def test_model_class_info():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    circle = m.class_info("LCircle;")
    assert circle is not None
    assert circle.descriptor == "LCircle;"
    assert circle.superclass == "LAbstractShape;"
    assert circle.method_count > 0
    assert circle.interfaces == []
    assert m.class_info("LAbstractShape;").interfaces == ["LShape;"]
    # An interface's own superclass is java.lang.Object.
    shape = m.class_info("LShape;")
    assert shape.superclass == "Ljava/lang/Object;"
    # A missing class is None, like every other lookup.
    assert m.class_info("Lno/such/Class;") is None
    assert not hasattr(m, "class_interfaces")


def test_model_write_round_trip():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    data = m.write()
    assert isinstance(data, bytes) and len(data) > 0
    again = dexpp.Model.read(data)
    assert len(again) == len(m)
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
    assert len(work) == 0
    # The clone was drained; the original is untouched.
    assert len(src) == len(descriptors)


def test_model_copy_remove_and_errors():
    src = dexpp.Model.read_file("tests/data/shapes.dex")
    dst = dexpp.Model(src.version)
    dst.copy_class(src, "LCircle;")
    assert "LCircle;" in src  # copy left the source intact
    assert "LCircle;" in dst

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
    assert len(dst) == 0


def test_model_rename_types():
    m = dexpp.Model.read_file("tests/data/shapes.dex")
    m.rename_types({"LCircle;": "Lcom/x/Round;"})
    assert "LCircle;" not in m
    assert "Lcom/x/Round;" in m
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
    assert len(m) == 0
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
        assert len(reread) == len(m)


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
        lambda: dexpp.Apk.from_bytes(b"not an apk"),
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
        dexpp.Method: ["calls", "field_accesses", "string_loads", "type_uses", "disassemble"],
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
    with open("tests/data/sample.apk", "rb") as f:
        assert dexpp.Apk.from_bytes(data=f.read(), label="sample.apk").entries() == apk.entries()
    assert apk.analysis(threads=1).find_class(descriptor="LTestDex;") is not None
    app_name = apk.resources.id_of(type="string", entry="app_name")
    assert apk.resolve_string(reference_id=app_name) == "Dexpp Sample"
    assert len(apk.manifest.components_of(kind=dexpp.ComponentKind.ACTIVITY)) == 1

    with open("tests/data/shapes.dex", "rb") as f:
        model = dexpp.Model.read(data=f.read(), debug_info=False)
    assert model.class_info(descriptor="LCircle;") is not None
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
    dexpp.CallNodeKind: ["DEFINED", "EXTERNAL", "CALL_SITE"],
    dexpp.CfgEdgeKind: [
        "FALLTHROUGH", "GOTO", "BRANCH_TAKEN", "BRANCH_NOT_TAKEN", "SWITCH_CASE",
        "SWITCH_DEFAULT", "EXCEPTION", "EXCEPTION_CATCH_ALL",
    ],
    dexpp.TypeUseKind: [
        "CONST_CLASS", "CHECK_CAST", "INSTANCE_OF", "NEW_INSTANCE", "NEW_ARRAY",
        "FILLED_NEW_ARRAY",
    ],
    dexpp.MethodHandleKind: [
        "STATIC_PUT", "STATIC_GET", "INSTANCE_PUT", "INSTANCE_GET", "INVOKE_STATIC",
        "INVOKE_INSTANCE", "INVOKE_CONSTRUCTOR", "INVOKE_DIRECT", "INVOKE_INTERFACE",
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
    assert builder[0].kind is dexpp.TypeUseKind.NEW_INSTANCE
    noise = next(m for m in pets.find_class("LKennel;").methods if m.name == "noise")
    assert noise.calls()[0].kind is dexpp.InvokeKind.INTERFACE

    fibonacci = dexpp.AnalysisContext.from_dex("tests/data/fibonacci.dex")
    methods = {m.name: m for m in fibonacci.find_class("LFibonacci;").methods}
    kinds = {e.kind for b in methods["compute"].cfg.blocks for e in b.successors}
    assert {type(k) for k in kinds} == {dexpp.CfgEdgeKind}
    assert dexpp.CfgEdgeKind.BRANCH_TAKEN in kinds
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
        lambda: manifest.components_of(dexpp.CfgEdgeKind.GOTO),
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
