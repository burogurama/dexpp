package io.github.burogurama.dexpp;

import java.util.Objects;

/**
 * One instruction of a method as readable text. {@link DexMethod#disassemble()} returns them.
 *
 * @param offset the offset of the instruction in the method's code, in code units: the same
 *     number as the instruction's {@link Instruction#offset()}
 * @param text the instruction with every reference named, in the style of smali and dexdump,
 *     such as {@code "invoke-virtual {v0}, Ljava/lang/Object;->hashCode()I"}
 */
public record DisassemblyLine(int offset, String text) {
    /** Checks that the text is given. */
    public DisassemblyLine {
        Objects.requireNonNull(text, "text");
    }
}
