package io.github.burogurama.dexpp;

import static io.github.burogurama.dexpp.TestData.cls;
import static io.github.burogurama.dexpp.TestData.dex;
import static io.github.burogurama.dexpp.TestData.method;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.Set;

/** Decoded instructions, control-flow graphs, try blocks, registers and references. */
class CodeTest {
    @Test
    void disassemble() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            DexMethod main = method(cls(ctx, "LTestDex;"), "main");
            assertEquals(
                    List.of(
                            new DisassemblyLine(
                                    0,
                                    "sget-object v1,"
                                            + " Ljava/lang/System;->out:Ljava/io/PrintStream;"),
                            new DisassemblyLine(
                                    2, "invoke-static {}, LTestDex;->helloDex()Ljava/lang/String;"),
                            new DisassemblyLine(5, "move-result-object v0"),
                            new DisassemblyLine(
                                    6,
                                    "invoke-virtual {v1, v0},"
                                            + " Ljava/io/PrintStream;->println(Ljava/lang/String;)V"),
                            new DisassemblyLine(9, "return-void")),
                    main.disassemble());
            List<Instruction> insns = main.instructions();
            List<DisassemblyLine> lines = main.disassemble();
            for (int i = 0; i < insns.size(); i++) {
                assertEquals(insns.get(i).offset(), lines.get(i).offset());
            }
        }
        try (AnalysisContext shapes = dex("shapes.dex")) {
            assertEquals(List.of(), method(cls(shapes, "LShape;"), "area").disassemble());
        }
        try (AnalysisContext handles = dex("handles.dex")) {
            List<DisassemblyLine> run = method(cls(handles, "LHandles;"), "run").disassemble();
            assertEquals(7, run.size());
            assertEquals(
                    "const-method-handle v2, invoke-static@LHandles;->target(I)I",
                    run.get(3).text());
            assertTrue(
                    run.get(5).text().startsWith("invoke-custom {v1}, call_site@0(\"apply\", (I)V)@"),
                    run.get(5).text());
        }
        try (AnalysisContext multi = dex("multistring.dex")) {
            boolean escaped = false;
            for (DisassemblyLine line :
                    method(cls(multi, "LMultiStringTest;"), "<clinit>").disassemble()) {
                escaped |= line.text().endsWith("\"test\\u0000embedded\"");
            }
            assertTrue(escaped, "the NUL in a string is escaped");
        }
    }

    @Test
    void instructions() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            DexClass cls = cls(ctx, "LTestDex;");

            List<Instruction> hello = method(cls, "helloDex").instructions();
            assertEquals(2, hello.size());
            ConstStringInstruction cs =
                    assertInstanceOf(ConstStringInstruction.class, hello.get(0));
            assertEquals(0, cs.offset());
            assertEquals(2, cs.size());
            ReturnInstruction ret = assertInstanceOf(ReturnInstruction.class, hello.get(1));
            assertEquals(OptionalInt.of(cs.dest()), ret.reg());

            // sget-object v1, field@0; invoke-static {}, method@1; move-result-object v0;
            // invoke-virtual {v1, v0}, method@3; return-void
            List<Instruction> main = method(cls, "main").instructions();
            assertEquals(5, main.size());
            FieldInstruction field = assertInstanceOf(FieldInstruction.class, main.get(0));
            assertTrue(field.isStatic());
            assertFalse(field.isWrite());
            assertEquals(OptionalInt.empty(), field.objectReg());
            assertEquals(1, field.valueReg());
            assertEquals(
                    List.of(), assertInstanceOf(InvokeInstruction.class, main.get(1)).argRegs());
            MoveInstruction move = assertInstanceOf(MoveInstruction.class, main.get(2));
            assertEquals(0, move.dest());
            assertEquals(OptionalInt.empty(), move.src(), "move-result names no source");
            assertEquals(OperandWidth.SINGLE, move.width());
            InvokeInstruction call = assertInstanceOf(InvokeInstruction.class, main.get(3));
            assertEquals(List.of(1, 0), call.argRegs());
            assertEquals(
                    OptionalInt.empty(),
                    assertInstanceOf(ReturnInstruction.class, main.get(4)).reg());

            List<String> text = new ArrayList<>();
            for (Instruction insn : main) {
                text.add(Dexpp.toString(insn));
            }
            assertEquals("sget-object v1, field@0", text.get(0));
            assertEquals("invoke-virtual method@3 {v1, v0}", text.get(3));
            assertEquals("return-void", text.get(4));
            assertEquals("invoke-virtual", Dexpp.opcodeName(call.opcode()));

            // Each instruction starts where the one before it ends.
            int offset = 0;
            for (Instruction insn : main) {
                assertEquals(offset, insn.offset());
                offset += insn.size();
            }
            assertThrows(UnsupportedOperationException.class, () -> main.add(main.get(0)));
        }
        // A method without code has no instructions.
        try (AnalysisContext pets = dex("polymorphic.dex")) {
            assertEquals(List.of(), method(cls(pets, "LAnimal;"), "sound").instructions());
        }
    }

    @Test
    void opcodeNames() {
        Map<Integer, String> names =
                Map.of(
                        0x00,
                        "nop",
                        0x1a,
                        "const-string",
                        0x6e,
                        "invoke-virtual",
                        0x73,
                        "unused-73");
        for (Map.Entry<Integer, String> e : names.entrySet()) {
            assertEquals(e.getValue(), Dexpp.opcodeName(e.getKey()));
        }
        assertEquals("invoke-virtual", Dexpp.opcodeName(0x16e), "only the low 8 bits count");
        try (AnalysisContext ctx = dex("classes.dex")) {
            List<String> rendered = new ArrayList<>();
            for (Instruction insn : method(cls(ctx, "LTestDex;"), "helloDex").instructions()) {
                rendered.add(Dexpp.toString(insn));
            }
            assertTrue(rendered.get(0).startsWith("const-string v"));
            assertTrue(rendered.get(0).contains("string@"));
            assertTrue(rendered.get(1).startsWith("return-object v"));
        }
    }

    /**
     * Dexpp.toString writes the Java record back into a C struct. Rendering every instruction of
     * every fixture checks the conversion in both directions for every kind the fixtures hold.
     */
    @Test
    void toStringOfEveryInstruction() {
        Set<Class<?>> kinds = new HashSet<>();
        for (String name :
                List.of(
                        "classes.dex",
                        "fibonacci.dex",
                        "trycatch.dex",
                        "shapes.dex",
                        "polymorphic.dex",
                        "annotated.dex",
                        "inherit.dex",
                        "multistring.dex",
                        "casts.dex",
                        "handles.dex",
                        "switches.dex")) {
            try (AnalysisContext ctx = dex(name)) {
                for (DexClass c : ctx.classes()) {
                    for (DexMethod m : c.methods()) {
                        for (Instruction insn : m.instructions()) {
                            kinds.add(insn.getClass());
                            String text = Dexpp.toString(insn);
                            assertTrue(text.startsWith(Dexpp.opcodeName(insn.opcode())), text);
                        }
                    }
                }
            }
        }
        assertTrue(
                kinds.size() >= 15, "the fixtures hold only " + kinds.size() + " kinds: " + kinds);
    }

    /** Instructions made by hand render like decoded ones. */
    @Test
    void toStringOfHandMadeInstructions() {
        Map<Instruction, String> cases =
                Map.ofEntries(
                        Map.entry(new GotoInstruction(0, 0x28, 1, -4), "goto -4"),
                        Map.entry(new IfZeroInstruction(0, 0x38, 2, 6, 3), "if-eqz v3, +6"),
                        Map.entry(
                                new InvokeInstruction(0, 0x71, 3, 9, List.of(2, 3)),
                                "invoke-static method@9 {v2, v3}"),
                        Map.entry(
                                new SwitchInstruction(
                                        0,
                                        0x2b,
                                        3,
                                        8,
                                        1,
                                        true,
                                        List.of(new SwitchCase(1, 4), new SwitchCase(2, 6))),
                                "packed-switch v1, +8"),
                        Map.entry(
                                new MoveInstruction(
                                        0, 0x01, 1, 2, OptionalInt.of(3), OperandWidth.SINGLE),
                                "move v2, v3"),
                        Map.entry(
                                new MoveInstruction(
                                        0, 0x0d, 1, 4, OptionalInt.empty(), OperandWidth.SINGLE),
                                "move-exception v4"),
                        Map.entry(
                                new ReturnInstruction(
                                        0, 0x0e, 1, OptionalInt.empty(), OperandWidth.SINGLE),
                                "return-void"),
                        Map.entry(new NopInstruction(0, 0x00, 1), "nop"),
                        Map.entry(new InvalidInstruction(0, 0x3e, 1), "unused-3e"),
                        Map.entry(new ArrayLengthInstruction(0, 0x21, 1, 1, 5), "array-length v1, v5"),
                        Map.entry(
                                new InvokePolymorphicInstruction(0, 0xfa, 4, 5, 2, List.of(4, 5)),
                                "invoke-polymorphic method@5 proto@2 {v4, v5}"),
                        Map.entry(
                                new ConstMethodHandleInstruction(0, 0xfe, 2, 2, 7),
                                "const-method-handle v2, method_handle@7"),
                        Map.entry(
                                new ConstMethodTypeInstruction(0, 0xff, 2, 3, 1),
                                "const-method-type v3, proto@1"));
        for (Map.Entry<Instruction, String> c : cases.entrySet()) {
            assertEquals(c.getValue(), Dexpp.toString(c.getKey()), c.getKey().toString());
        }
    }

    @Test
    void handlesAndPolymorphicInstructions() {
        // handles.dex, Handles.run: invoke-polymorphic {v4, v5}; move-result-object v0;
        // array-length v1, v5; const-method-handle v2; const-method-type v3;
        // invoke-custom {v1}; return-object v0. Handles.spread: invoke-polymorphic/range.
        try (AnalysisContext ctx = dex("handles.dex")) {
            DexClass cls = cls(ctx, "LHandles;");
            List<Instruction> run = method(cls, "run").instructions();
            assertEquals(7, run.size());
            InvokePolymorphicInstruction poly =
                    assertInstanceOf(InvokePolymorphicInstruction.class, run.get(0));
            assertEquals(List.of(4, 5), poly.argRegs());
            assertEquals(
                    OptionalInt.empty(), assertInstanceOf(MoveInstruction.class, run.get(1)).src());
            ArrayLengthInstruction length =
                    assertInstanceOf(ArrayLengthInstruction.class, run.get(2));
            assertEquals(1, length.dest());
            assertEquals(5, length.arrayReg());
            assertEquals(
                    2, assertInstanceOf(ConstMethodHandleInstruction.class, run.get(3)).dest());
            assertEquals(3, assertInstanceOf(ConstMethodTypeInstruction.class, run.get(4)).dest());
            InvokePolymorphicInstruction range =
                    assertInstanceOf(
                            InvokePolymorphicInstruction.class,
                            method(cls, "spread").instructions().get(0));
            assertEquals(List.of(0, 1, 2, 3, 4, 5), range.argRegs());
            assertTrue(range.protoIndex() != poly.protoIndex());

            List<MethodCall> calls = method(cls, "run").calls();
            assertEquals(1, calls.size());
            assertEquals(InvokeKind.POLYMORPHIC, calls.get(0).kind());
            assertEquals("invokeExact", calls.get(0).target().name());
        }
    }

    @Test
    void cfg() {
        try (AnalysisContext ctx = dex("fibonacci.dex")) {
            DexMethod compute = method(cls(ctx, "LFibonacci;"), "compute");
            Cfg cfg = compute.cfg();
            assertFalse(cfg.isEmpty());
            assertTrue(cfg.size() >= 2);
            assertEquals(cfg.blocks().size(), cfg.size());
            BasicBlock entry = cfg.entry().orElseThrow();
            assertEquals(0, entry.index());
            assertEquals(0, entry.startOffset());
            assertEquals(0, cfg.blockAtOffset(0).orElseThrow().index());
            assertEquals(Optional.empty(), cfg.blockAtOffset(99999));

            Set<CfgEdgeKind> kinds = EnumSet.noneOf(CfgEdgeKind.class);
            List<Instruction> insns = compute.instructions();
            for (int i = 0; i < cfg.size(); i++) {
                BasicBlock b = cfg.blocks().get(i);
                assertEquals(i, b.index());
                assertTrue(b.insnCount() > 0);
                assertEquals(b.startOffset(), insns.get(b.firstInsn()).offset());
                assertEquals(b.index(), cfg.blockAtOffset(b.startOffset()).orElseThrow().index());
                for (CfgEdge e : b.successors()) {
                    kinds.add(e.kind());
                    assertTrue(e.targetBlock() < cfg.size());
                    assertTrue(cfg.blocks().get(e.targetBlock()).predecessors().contains(i));
                }
            }
            assertTrue(kinds.contains(CfgEdgeKind.BRANCH_TAKEN), kinds.toString());
            assertTrue(kinds.contains(CfgEdgeKind.GOTO), kinds.toString());
        }
        // A method without code has an empty graph.
        try (AnalysisContext pets = dex("polymorphic.dex")) {
            Cfg empty = method(cls(pets, "LAnimal;"), "sound").cfg();
            assertTrue(empty.isEmpty());
            assertEquals(0, empty.size());
            assertEquals(Optional.empty(), empty.entry());
        }
    }

    @Test
    void tryBlocksAndExceptionEdges() {
        try (AnalysisContext ctx = dex("trycatch.dex")) {
            DexClass cls = cls(ctx, "LTryCatch;");
            DexMethod divide = method(cls, "divide");
            List<TryBlock> tries = divide.tryBlocks();
            assertEquals(1, tries.size());
            TryBlock tryBlock = tries.get(0);
            assertTrue(tryBlock.startOffset() < tryBlock.endOffset());
            assertFalse(tryBlock.hasCatchAll());
            assertEquals(OptionalInt.empty(), tryBlock.catchAllOffset());
            assertEquals(1, tryBlock.handlers().size());
            CatchHandler handler = tryBlock.handlers().get(0);
            assertEquals("Ljava/lang/ArithmeticException;", handler.typeDescriptor());

            boolean exception = false;
            Cfg cfg = divide.cfg();
            for (BasicBlock b : cfg.blocks()) {
                for (CfgEdge e : b.successors()) {
                    assertEquals(OptionalInt.empty(), e.caseKey());
                    if (e.kind() == CfgEdgeKind.EXCEPTION) {
                        exception = true;
                        assertEquals(
                                Optional.of("Ljava/lang/ArithmeticException;"), e.catchType());
                        assertTrue(cfg.blocks().get(e.targetBlock()).isHandlerEntry());
                        assertEquals(
                                e.targetBlock(),
                                cfg.blockAtOffset(handler.handlerOffset()).orElseThrow().index());
                    } else {
                        assertEquals(Optional.empty(), e.catchType());
                    }
                }
            }
            assertTrue(exception, "divide has no exception edge");

            boolean catchAll = false;
            DexMethod safeParse = method(cls, "safeParse");
            for (TryBlock t : safeParse.tryBlocks()) {
                catchAll = catchAll || t.hasCatchAll();
            }
            assertTrue(catchAll, "safeParse has no catch-all handler");
            // The catch-all handler has edges of its own kind, with no catch type.
            boolean catchAllEdge = false;
            for (BasicBlock b : safeParse.cfg().blocks()) {
                for (CfgEdge e : b.successors()) {
                    if (e.kind() == CfgEdgeKind.EXCEPTION_CATCH_ALL) {
                        catchAllEdge = true;
                        assertEquals(Optional.empty(), e.catchType());
                    }
                }
            }
            assertTrue(catchAllEdge, "safeParse has no catch-all edge");
            assertEquals(List.of(), method(cls, "<init>").tryBlocks());
        }
    }

    @Test
    void switchCaseEdges() {
        try (AnalysisContext ctx = dex("switches.dex")) {
            DexMethod packed = method(ctx.findClass("LSwitches;").orElseThrow(), "packed");
            List<Integer> keys = new ArrayList<>();
            int defaults = 0;
            for (CfgEdge e : packed.cfg().blocks().get(0).successors()) {
                if (e.kind() == CfgEdgeKind.SWITCH_CASE) {
                    keys.add(e.caseKey().orElseThrow());
                } else if (e.kind() == CfgEdgeKind.SWITCH_DEFAULT) {
                    assertEquals(OptionalInt.empty(), e.caseKey());
                    defaults++;
                }
            }
            assertEquals(List.of(10, 11, 12, 13), keys);
            assertEquals(1, defaults);
        }
    }

    @Test
    void parameterRegisters() {
        try (AnalysisContext ctx = dex("fibonacci.dex")) {
            DexClass cls = cls(ctx, "LFibonacci;");
            DexMethod compute = method(cls, "compute"); // an instance method with an int parameter
            assertEquals(11, compute.registerCount());
            assertEquals(
                    List.of(
                            new ParameterRegister(9, "LFibonacci;", true, false),
                            new ParameterRegister(10, "I", false, false)),
                    compute.parameterRegisters());
            DexMethod classify = method(cls, "classify"); // a static method with a long parameter
            assertEquals(
                    List.of(new ParameterRegister(3, "J", false, true)),
                    classify.parameterRegisters());
        }
    }

    @Test
    void callsAndOtherReferences() {
        try (AnalysisContext ctx = dex("classes.dex")) {
            DexClass cls = cls(ctx, "LTestDex;");
            DexMethod main = method(cls, "main");
            assertEquals(
                    List.of(
                            new MethodCall(
                                    2,
                                    new MethodId("LTestDex;", "helloDex", "()Ljava/lang/String;"),
                                    InvokeKind.STATIC),
                            new MethodCall(
                                    6,
                                    new MethodId(
                                            "Ljava/io/PrintStream;",
                                            "println",
                                            "(Ljava/lang/String;)V"),
                                    InvokeKind.VIRTUAL)),
                    main.calls());
            FieldId out = new FieldId("Ljava/lang/System;", "out", "Ljava/io/PrintStream;");
            assertEquals(List.of(new FieldAccess(0, out, false, true)), main.fieldAccesses());
            assertEquals(
                    List.of(new StringLoad(0, "Hello Dex++!!")),
                    method(cls, "helloDex").stringLoads());
            assertEquals(List.of(), main.stringLoads());
        }
        // A string constant with a NUL character keeps it.
        try (AnalysisContext multi = dex("multistring.dex")) {
            boolean found = false;
            for (StringLoad s : method(cls(multi, "LMultiStringTest;"), "<clinit>").stringLoads()) {
                found = found || s.value().equals("test\0embedded");
            }
            assertTrue(found);
        }
        try (AnalysisContext pets = dex("polymorphic.dex")) {
            List<TypeUse> uses = method(cls(pets, "LDog;"), "describe").typeUses();
            assertFalse(uses.isEmpty());
            assertEquals("Ljava/lang/StringBuilder;", uses.get(0).descriptor());
            assertEquals(TypeUseKind.NEW_INSTANCE, uses.get(0).kind());
        }
    }

    @Test
    void enumsMatchTheCApi() {
        assertEquals(InvokeKind.INTERFACE, InvokeKind.of(4));
        assertEquals(6, InvokeKind.CUSTOM.value());
        assertEquals(EdgeOrigin.INHERITED_RESOLUTION, EdgeOrigin.of(1));
        assertEquals(CfgEdgeKind.SWITCH_DEFAULT, CfgEdgeKind.of(5));
        assertEquals(TypeUseKind.FILLED_NEW_ARRAY, TypeUseKind.of(5));
        assertEquals(OperandWidth.PAIR, OperandWidth.of(1));
        assertEquals(AnnotationVisibility.SYSTEM, AnnotationVisibility.of(2));
        assertEquals(ComponentKind.PROVIDER, ComponentKind.of(3));
        assertThrows(IllegalArgumentException.class, () -> ComponentKind.of(9));
        assertThrows(IllegalArgumentException.class, () -> InvokeKind.of(-1));
    }
}
