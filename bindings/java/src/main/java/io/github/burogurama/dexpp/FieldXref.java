package io.github.burogurama.dexpp;

/**
 * One instruction that reads or writes a field, as {@link Xrefs} finds it.
 *
 * @param referrer the number of the method that holds the instruction; see {@link
 *     Xrefs#referrerId(int)}
 * @param codeOffset the offset of the instruction, in code units
 * @param isWrite true for a write, false for a read
 * @param isStatic true for a static field access
 */
public record FieldXref(int referrer, int codeOffset, boolean isWrite, boolean isStatic) {}
