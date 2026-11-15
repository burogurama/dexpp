package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.OptionalInt;
import java.util.OptionalLong;

/**
 * Converts the code structs of the C API: instructions in both directions, and the control-flow
 * graph, try blocks and references of a method into Java records.
 */
final class Code {
    private Code() {}

    // The DexppInstructionKind values.
    private static final int NOP = 0;
    private static final int GOTO = 1;
    private static final int IF = 2;
    private static final int IF_ZERO = 3;
    private static final int SWITCH = 4;
    private static final int RETURN = 5;
    private static final int THROW = 6;
    private static final int MOVE = 7;
    private static final int CONST = 8;
    private static final int CONST_STRING = 9;
    private static final int CONST_CLASS = 10;
    private static final int CONST_HANDLE = 11;
    private static final int TYPE = 12;
    private static final int FILLED_NEW_ARRAY = 13;
    private static final int MONITOR = 14;
    private static final int FILL_ARRAY = 15;
    private static final int COMPARE = 16;
    private static final int ARRAY_OP = 17;
    private static final int FIELD = 18;
    private static final int INVOKE = 19;
    private static final int INVOKE_CUSTOM = 20;
    private static final int UNARY_OP = 21;
    private static final int BINARY_OP = 22;
    private static final int BINARY_LIT = 23;

    // ---- C to Java -------------------------------------------------------------

    /** Copies the {@code DexppInstruction} at {@code b}. */
    static Instruction instruction(MemorySegment m, long b) {
        int offset = Mem.i32(m, b + Layouts.Instruction.OFFSET);
        int opcode = Mem.u8(m, b + Layouts.Instruction.OPCODE);
        int size = Mem.u8(m, b + Layouts.Instruction.SIZE);
        return switch (Mem.i32(m, b + Layouts.Instruction.KIND)) {
            case GOTO -> new GotoInstruction(offset, opcode, size, branch(m, b));
            case IF ->
                    new IfInstruction(
                            offset,
                            opcode,
                            size,
                            branch(m, b),
                            Mem.u8(m, b + Layouts.Instruction.REG_A),
                            Mem.u8(m, b + Layouts.Instruction.REG_B));
            case IF_ZERO -> new IfZeroInstruction(offset, opcode, size, branch(m, b), reg(m, b));
            case SWITCH ->
                    new SwitchInstruction(
                            offset,
                            opcode,
                            size,
                            payload(m, b),
                            reg(m, b),
                            Mem.bool(m, b + Layouts.Instruction.IS_PACKED),
                            Mem.array(
                                    Mem.pointer(m, b + Layouts.Instruction.CASES),
                                    Mem.i64(m, b + Layouts.Instruction.CASE_COUNT),
                                    Layouts.SwitchCase.LAYOUT,
                                    (c, at) ->
                                            new SwitchCase(
                                                    Mem.i32(c, at + Layouts.SwitchCase.KEY),
                                                    Mem.i32(c, at + Layouts.SwitchCase.TARGET))));
            case RETURN ->
                    new ReturnInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.bool(m, b + Layouts.Instruction.HAS_REG)
                                    ? OptionalInt.of(reg(m, b))
                                    : OptionalInt.empty(),
                            width(m, b + Layouts.Instruction.WIDTH));
            case THROW -> new ThrowInstruction(offset, opcode, size, reg(m, b));
            case MOVE ->
                    new MoveInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.u16(m, b + Layouts.Instruction.SRC),
                            width(m, b + Layouts.Instruction.WIDTH));
            case CONST ->
                    new ConstInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.i64(m, b + Layouts.Instruction.VALUE),
                            width(m, b + Layouts.Instruction.WIDTH));
            case CONST_STRING ->
                    new ConstStringInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.i32(m, b + Layouts.Instruction.STRING_INDEX));
            case CONST_CLASS ->
                    new ConstClassInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.i32(m, b + Layouts.Instruction.TYPE_INDEX));
            case CONST_HANDLE ->
                    new ConstHandleInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.i32(m, b + Layouts.Instruction.INDEX));
            case TYPE ->
                    new TypeInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.i32(m, b + Layouts.Instruction.TYPE_INDEX),
                            dest(m, b),
                            Mem.bool(m, b + Layouts.Instruction.HAS_SRC)
                                    ? OptionalInt.of(Mem.u16(m, b + Layouts.Instruction.SRC))
                                    : OptionalInt.empty());
            case FILLED_NEW_ARRAY ->
                    new FilledNewArrayInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.i32(m, b + Layouts.Instruction.TYPE_INDEX),
                            args(m, b));
            case MONITOR ->
                    new MonitorInstruction(
                            offset,
                            opcode,
                            size,
                            reg(m, b),
                            Mem.bool(m, b + Layouts.Instruction.IS_ENTER));
            case FILL_ARRAY ->
                    new FillArrayInstruction(offset, opcode, size, reg(m, b), payload(m, b));
            case COMPARE ->
                    new CompareInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.u8(m, b + Layouts.Instruction.SRC_A),
                            Mem.u8(m, b + Layouts.Instruction.SRC_B),
                            width(m, b + Layouts.Instruction.SRC_WIDTH));
            case ARRAY_OP ->
                    new ArrayOpInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.u8(m, b + Layouts.Instruction.VALUE_REG),
                            Mem.u8(m, b + Layouts.Instruction.ARRAY_REG),
                            Mem.u8(m, b + Layouts.Instruction.INDEX_REG),
                            Mem.bool(m, b + Layouts.Instruction.IS_WRITE),
                            width(m, b + Layouts.Instruction.WIDTH));
            case FIELD ->
                    new FieldInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.i32(m, b + Layouts.Instruction.FIELD_INDEX),
                            Mem.u8(m, b + Layouts.Instruction.VALUE_REG),
                            Mem.bool(m, b + Layouts.Instruction.HAS_OBJECT_REG)
                                    ? OptionalInt.of(Mem.u8(m, b + Layouts.Instruction.OBJECT_REG))
                                    : OptionalInt.empty(),
                            Mem.bool(m, b + Layouts.Instruction.IS_STATIC),
                            Mem.bool(m, b + Layouts.Instruction.IS_WRITE),
                            width(m, b + Layouts.Instruction.WIDTH));
            case INVOKE ->
                    new InvokeInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.i32(m, b + Layouts.Instruction.METHOD_INDEX),
                            args(m, b));
            case INVOKE_CUSTOM ->
                    new InvokeCustomInstruction(
                            offset,
                            opcode,
                            size,
                            Mem.i32(m, b + Layouts.Instruction.CALL_SITE_INDEX),
                            args(m, b));
            case UNARY_OP ->
                    new UnaryOpInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.u16(m, b + Layouts.Instruction.SRC),
                            width(m, b + Layouts.Instruction.DEST_WIDTH),
                            width(m, b + Layouts.Instruction.SRC_WIDTH));
            case BINARY_OP ->
                    new BinaryOpInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.u8(m, b + Layouts.Instruction.SRC_A),
                            Mem.u8(m, b + Layouts.Instruction.SRC_B),
                            width(m, b + Layouts.Instruction.WIDTH));
            case BINARY_LIT ->
                    new BinaryLitInstruction(
                            offset,
                            opcode,
                            size,
                            dest(m, b),
                            Mem.u16(m, b + Layouts.Instruction.SRC),
                            Mem.i32(m, b + Layouts.Instruction.LITERAL));
            // NOP, and a kind this package does not know yet, from a newer library.
            default -> new NopInstruction(offset, opcode, size);
        };
    }

    private static int branch(MemorySegment m, long b) {
        return Mem.i32(m, b + Layouts.Instruction.BRANCH_OFFSET);
    }

    private static int payload(MemorySegment m, long b) {
        return Mem.i32(m, b + Layouts.Instruction.PAYLOAD_OFFSET);
    }

    private static int reg(MemorySegment m, long b) {
        return Mem.u8(m, b + Layouts.Instruction.REG);
    }

    private static int dest(MemorySegment m, long b) {
        return Mem.u16(m, b + Layouts.Instruction.DEST);
    }

    private static List<Integer> args(MemorySegment m, long b) {
        return Mem.registers(
                Mem.pointer(m, b + Layouts.Instruction.ARG_REGS),
                Mem.i64(m, b + Layouts.Instruction.ARG_COUNT));
    }

    private static OperandWidth width(MemorySegment m, long at) {
        return Mem.i32(m, at) == OperandWidth.PAIR.value()
                ? OperandWidth.PAIR
                : OperandWidth.SINGLE;
    }

    // ---- Java to C, for dexpp_instruction_to_string --------------------------------

    /** Writes an instruction into a new {@code DexppInstruction} in {@code arena}. */
    static MemorySegment toC(Arena arena, Instruction insn) {
        MemorySegment m = arena.allocate(Layouts.Instruction.LAYOUT); // zeroed
        m.set(JAVA_INT, Layouts.Instruction.OFFSET, insn.offset());
        m.set(JAVA_BYTE, Layouts.Instruction.OPCODE, (byte) insn.opcode());
        m.set(JAVA_BYTE, Layouts.Instruction.SIZE, (byte) insn.size());
        int kind =
                switch (insn) {
                    case NopInstruction unused -> NOP;
                    case GotoInstruction i -> {
                        setInt(m, Layouts.Instruction.BRANCH_OFFSET, i.branchOffset());
                        yield GOTO;
                    }
                    case IfInstruction i -> {
                        setInt(m, Layouts.Instruction.BRANCH_OFFSET, i.branchOffset());
                        setU8(m, Layouts.Instruction.REG_A, i.regA());
                        setU8(m, Layouts.Instruction.REG_B, i.regB());
                        yield IF;
                    }
                    case IfZeroInstruction i -> {
                        setInt(m, Layouts.Instruction.BRANCH_OFFSET, i.branchOffset());
                        setU8(m, Layouts.Instruction.REG, i.reg());
                        yield IF_ZERO;
                    }
                    case SwitchInstruction i -> {
                        setInt(m, Layouts.Instruction.PAYLOAD_OFFSET, i.payloadOffset());
                        setU8(m, Layouts.Instruction.REG, i.reg());
                        setBool(m, Layouts.Instruction.IS_PACKED, i.isPacked());
                        if (!i.cases().isEmpty()) {
                            MemorySegment cases =
                                    arena.allocate(Layouts.SwitchCase.LAYOUT, i.cases().size());
                            long step = Layouts.SwitchCase.LAYOUT.byteSize();
                            for (int k = 0; k < i.cases().size(); k++) {
                                cases.set(
                                        JAVA_INT,
                                        k * step + Layouts.SwitchCase.KEY,
                                        i.cases().get(k).key());
                                cases.set(
                                        JAVA_INT,
                                        k * step + Layouts.SwitchCase.TARGET,
                                        i.cases().get(k).target());
                            }
                            m.set(ADDRESS, Layouts.Instruction.CASES, cases);
                            m.set(JAVA_LONG, Layouts.Instruction.CASE_COUNT, i.cases().size());
                        }
                        yield SWITCH;
                    }
                    case ReturnInstruction i -> {
                        setBool(m, Layouts.Instruction.HAS_REG, i.reg().isPresent());
                        setU8(m, Layouts.Instruction.REG, i.reg().orElse(0));
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield RETURN;
                    }
                    case ThrowInstruction i -> {
                        setU8(m, Layouts.Instruction.REG, i.reg());
                        yield THROW;
                    }
                    case MoveInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setU16(m, Layouts.Instruction.SRC, i.src());
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield MOVE;
                    }
                    case ConstInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        m.set(JAVA_LONG, Layouts.Instruction.VALUE, i.value());
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield CONST;
                    }
                    case ConstStringInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setInt(m, Layouts.Instruction.STRING_INDEX, i.stringIndex());
                        yield CONST_STRING;
                    }
                    case ConstClassInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setInt(m, Layouts.Instruction.TYPE_INDEX, i.typeIndex());
                        yield CONST_CLASS;
                    }
                    case ConstHandleInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setInt(m, Layouts.Instruction.INDEX, i.index());
                        yield CONST_HANDLE;
                    }
                    case TypeInstruction i -> {
                        setInt(m, Layouts.Instruction.TYPE_INDEX, i.typeIndex());
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setBool(m, Layouts.Instruction.HAS_SRC, i.src().isPresent());
                        setU16(m, Layouts.Instruction.SRC, i.src().orElse(0));
                        yield TYPE;
                    }
                    case FilledNewArrayInstruction i -> {
                        setInt(m, Layouts.Instruction.TYPE_INDEX, i.typeIndex());
                        setArgs(arena, m, i.argRegs());
                        yield FILLED_NEW_ARRAY;
                    }
                    case MonitorInstruction i -> {
                        setU8(m, Layouts.Instruction.REG, i.reg());
                        setBool(m, Layouts.Instruction.IS_ENTER, i.isEnter());
                        yield MONITOR;
                    }
                    case FillArrayInstruction i -> {
                        setU8(m, Layouts.Instruction.REG, i.reg());
                        setInt(m, Layouts.Instruction.PAYLOAD_OFFSET, i.payloadOffset());
                        yield FILL_ARRAY;
                    }
                    case CompareInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setU8(m, Layouts.Instruction.SRC_A, i.srcA());
                        setU8(m, Layouts.Instruction.SRC_B, i.srcB());
                        setWidth(m, Layouts.Instruction.SRC_WIDTH, i.srcWidth());
                        yield COMPARE;
                    }
                    case ArrayOpInstruction i -> {
                        setU8(m, Layouts.Instruction.VALUE_REG, i.valueReg());
                        setU8(m, Layouts.Instruction.ARRAY_REG, i.arrayReg());
                        setU8(m, Layouts.Instruction.INDEX_REG, i.indexReg());
                        setBool(m, Layouts.Instruction.IS_WRITE, i.isWrite());
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield ARRAY_OP;
                    }
                    case FieldInstruction i -> {
                        setInt(m, Layouts.Instruction.FIELD_INDEX, i.fieldIndex());
                        setU8(m, Layouts.Instruction.VALUE_REG, i.valueReg());
                        setBool(m, Layouts.Instruction.HAS_OBJECT_REG, i.objectReg().isPresent());
                        setU8(m, Layouts.Instruction.OBJECT_REG, i.objectReg().orElse(0));
                        setBool(m, Layouts.Instruction.IS_STATIC, i.isStatic());
                        setBool(m, Layouts.Instruction.IS_WRITE, i.isWrite());
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield FIELD;
                    }
                    case InvokeInstruction i -> {
                        setInt(m, Layouts.Instruction.METHOD_INDEX, i.methodIndex());
                        setArgs(arena, m, i.argRegs());
                        yield INVOKE;
                    }
                    case InvokeCustomInstruction i -> {
                        setInt(m, Layouts.Instruction.CALL_SITE_INDEX, i.callSiteIndex());
                        setArgs(arena, m, i.argRegs());
                        yield INVOKE_CUSTOM;
                    }
                    case UnaryOpInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setU16(m, Layouts.Instruction.SRC, i.src());
                        setWidth(m, Layouts.Instruction.DEST_WIDTH, i.destWidth());
                        setWidth(m, Layouts.Instruction.SRC_WIDTH, i.srcWidth());
                        yield UNARY_OP;
                    }
                    case BinaryOpInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setU8(m, Layouts.Instruction.SRC_A, i.srcA());
                        setU8(m, Layouts.Instruction.SRC_B, i.srcB());
                        setWidth(m, Layouts.Instruction.WIDTH, i.width());
                        yield BINARY_OP;
                    }
                    case BinaryLitInstruction i -> {
                        setU16(m, Layouts.Instruction.DEST, i.dest());
                        setU16(m, Layouts.Instruction.SRC, i.src());
                        setInt(m, Layouts.Instruction.LITERAL, i.literal());
                        yield BINARY_LIT;
                    }
                };
        m.set(JAVA_INT, Layouts.Instruction.KIND, kind);
        return m;
    }

    private static void setInt(MemorySegment m, long at, int value) {
        m.set(JAVA_INT, at, value);
    }

    private static void setU8(MemorySegment m, long at, int value) {
        m.set(JAVA_BYTE, at, (byte) value);
    }

    private static void setU16(MemorySegment m, long at, int value) {
        m.set(JAVA_SHORT, at, (short) value);
    }

    private static void setBool(MemorySegment m, long at, boolean value) {
        m.set(JAVA_INT, at, value ? 1 : 0);
    }

    private static void setWidth(MemorySegment m, long at, OperandWidth width) {
        m.set(JAVA_INT, at, width.value());
    }

    private static void setArgs(Arena arena, MemorySegment m, List<Integer> regs) {
        if (regs.isEmpty()) {
            return;
        }
        MemorySegment array = arena.allocate(JAVA_SHORT, regs.size());
        for (int k = 0; k < regs.size(); k++) {
            array.setAtIndex(JAVA_SHORT, k, (short) (int) regs.get(k));
        }
        m.set(ADDRESS, Layouts.Instruction.ARG_REGS, array);
        m.set(JAVA_LONG, Layouts.Instruction.ARG_COUNT, regs.size());
    }

    // ---- method data ------------------------------------------------------------------

    /** Copies the {@code DexppBasicBlock} at {@code b}. */
    static BasicBlock basicBlock(MemorySegment m, long b) {
        List<CfgEdge> successors =
                Mem.array(
                        Mem.pointer(m, b + Layouts.BasicBlock.SUCCESSORS),
                        Mem.i64(m, b + Layouts.BasicBlock.SUCCESSOR_COUNT),
                        Layouts.CfgEdge.LAYOUT,
                        (e, at) ->
                                new CfgEdge(
                                        Mem.i32(e, at + Layouts.CfgEdge.TARGET_BLOCK),
                                        CfgEdgeKind.of(Mem.i32(e, at + Layouts.CfgEdge.KIND)),
                                        Mem.bool(e, at + Layouts.CfgEdge.HAS_LABEL)
                                                ? OptionalLong.of(
                                                        Mem.i64(e, at + Layouts.CfgEdge.LABEL))
                                                : OptionalLong.empty()));
        return new BasicBlock(
                Mem.i32(m, b + Layouts.BasicBlock.INDEX),
                Mem.i32(m, b + Layouts.BasicBlock.START_OFFSET),
                Mem.i32(m, b + Layouts.BasicBlock.END_OFFSET),
                Mem.i32(m, b + Layouts.BasicBlock.FIRST_INSN),
                Mem.i32(m, b + Layouts.BasicBlock.INSN_COUNT),
                successors,
                Mem.indices(
                        Mem.pointer(m, b + Layouts.BasicBlock.PREDECESSORS),
                        Mem.i64(m, b + Layouts.BasicBlock.PREDECESSOR_COUNT)),
                Mem.bool(m, b + Layouts.BasicBlock.IS_HANDLER_ENTRY));
    }

    /** Copies the {@code DexppTryBlock} at {@code b}. */
    static TryBlock tryBlock(MemorySegment m, long b) {
        List<CatchHandler> handlers =
                Mem.array(
                        Mem.pointer(m, b + Layouts.TryBlock.HANDLERS),
                        Mem.i64(m, b + Layouts.TryBlock.HANDLER_COUNT),
                        Layouts.CatchHandler.LAYOUT,
                        (h, at) ->
                                new CatchHandler(
                                        Mem.string(h, at + Layouts.CatchHandler.TYPE_DESCRIPTOR),
                                        Mem.i32(h, at + Layouts.CatchHandler.HANDLER_OFFSET)));
        return new TryBlock(
                Mem.i32(m, b + Layouts.TryBlock.START_OFFSET),
                Mem.i32(m, b + Layouts.TryBlock.END_OFFSET),
                handlers,
                Mem.bool(m, b + Layouts.TryBlock.HAS_CATCH_ALL)
                        ? OptionalInt.of(Mem.i32(m, b + Layouts.TryBlock.CATCH_ALL_OFFSET))
                        : OptionalInt.empty());
    }
}
