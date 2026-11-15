"""The whole-program call graph and virtual dispatch."""

import dexpp

# --8<-- [start:graph]
ctx: dexpp.AnalysisContext = dexpp.AnalysisContext.from_dex("tests/data/polymorphic.dex")
graph: dexpp.CallGraph = ctx.call_graph
print(len(graph.nodes), "methods,", len(graph.edges), "edges")


def label(node: dexpp.CallNode) -> str:
    """A short name for a node: its class and its method name."""
    return node.id.class_descriptor + "->" + node.id.name
# --8<-- [end:graph]


# --8<-- [start:node]
noise_id: dexpp.MethodId = dexpp.MethodId("LKennel;", "noise", "(LAnimal;)Ljava/lang/String;")
noise: dexpp.CallNode | None = graph.find(noise_id)
assert noise is not None
print(label(noise), "resolved:", noise.resolved)

for edge in noise.out_edges:
    print("  calls", label(edge.callee), edge.kind.name, "at offset", edge.code_offset)
# --8<-- [end:node]

# --8<-- [start:dispatch]
sound_id: dexpp.MethodId = dexpp.MethodId("LAnimal;", "sound", "()Ljava/lang/String;")
sound: dexpp.CallNode | None = graph.find(sound_id)
assert sound is not None

targets: dexpp.CallNodeView = sound.dispatch_targets
callees: dexpp.CallNodeView = noise.callees()
print(sorted(label(target) for target in targets))
print(sorted(label(callee) for callee in callees))
# --8<-- [end:dispatch]

# --8<-- [start:callers]
dog_sound_id: dexpp.MethodId = dexpp.MethodId("LDog;", "sound", "()Ljava/lang/String;")
dog_sound: dexpp.CallNode | None = graph.find(dog_sound_id)
assert dog_sound is not None

callers: dexpp.CallNodeView = dog_sound.callers()
print(sorted(label(caller) for caller in callers))
# --8<-- [end:callers]

# --8<-- [start:external]
external: list[dexpp.CallNode] = [node for node in graph.nodes if node.resolved is False]
print(sorted(label(node) for node in external))

no_method: dexpp.Method | None = external[0].method
print(no_method)
# --8<-- [end:external]

# --8<-- [start:filter]
declared: dexpp.CallEdgeView = graph.edges.where(origin=dexpp.EdgeOrigin.DECLARED)
interface_calls: dexpp.CallEdgeView = graph.edges.where(kind=dexpp.InvokeKind.INTERFACE)
print(len(declared), "call sites,", len(interface_calls), "of them invoke-interface")
# --8<-- [end:filter]

# --8<-- [start:inherited]
inherited: dexpp.CallEdgeView = graph.edges.where(origin=dexpp.EdgeOrigin.INHERITED_RESOLUTION)
for edge in inherited:
    print(label(edge.caller), "resolves to", label(edge.callee))
# --8<-- [end:inherited]
