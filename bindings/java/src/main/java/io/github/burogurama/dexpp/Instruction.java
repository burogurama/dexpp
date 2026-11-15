package io.github.burogurama.dexpp;

/**
 * One decoded Dalvik instruction. It is one of 28 records, one per family of opcodes, the same
 * families as the C++ and Python APIs. A pattern-matching {@code switch} tells them apart:
 *
 * {@snippet :
 * String text = switch (insn) {
 *     case InvokeInstruction invoke -> "call, method index " + invoke.methodIndex();
 *     case ReturnInstruction ret -> "return";
 *     default -> Dexpp.toString(insn);
 * };
 * }
 *
 * <p>Pool indices ({@code stringIndex}, {@code typeIndex}, {@code fieldIndex}, {@code methodIndex},
 * ...) are raw indices into the tables of the DEX file. An instruction alone cannot turn them into
 * names: {@link DexMethod#calls()}, {@link DexMethod#fieldAccesses()}, {@link
 * DexMethod#stringLoads()} and {@link DexMethod#typeUses()} give the names. {@link
 * Dexpp#toString(Instruction)} writes an instruction as text.
 */
public sealed interface Instruction
        permits NopInstruction,
                GotoInstruction,
                IfInstruction,
                IfZeroInstruction,
                SwitchInstruction,
                ReturnInstruction,
                ThrowInstruction,
                MoveInstruction,
                ConstInstruction,
                ConstStringInstruction,
                ConstClassInstruction,
                ConstMethodHandleInstruction,
                ConstMethodTypeInstruction,
                TypeInstruction,
                FilledNewArrayInstruction,
                MonitorInstruction,
                FillArrayInstruction,
                CompareInstruction,
                ArrayOpInstruction,
                ArrayLengthInstruction,
                FieldInstruction,
                InvokeInstruction,
                InvokePolymorphicInstruction,
                InvokeCustomInstruction,
                UnaryOpInstruction,
                BinaryOpInstruction,
                BinaryLitInstruction,
                InvalidInstruction {
    /**
     * The position of the instruction in the method, in 16-bit code units from the start of the
     * code.
     *
     * @return the offset
     */
    int offset();

    /**
     * The raw opcode byte, from 0 to 255. {@link Dexpp#opcodeName} gives its name.
     *
     * @return the opcode
     */
    int opcode();

    /**
     * The length of the instruction in code units, from 1 to 5.
     *
     * @return the size
     */
    int size();
}
