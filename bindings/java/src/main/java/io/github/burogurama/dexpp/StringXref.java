package io.github.burogurama.dexpp;

/**
 * One {@code const-string} instruction, as {@link Xrefs} finds it.
 *
 * @param referrer the number of the method that holds the instruction; see {@link
 *     Xrefs#referrerId(int)}
 * @param codeOffset the offset of the instruction, in code units
 */
public record StringXref(int referrer, int codeOffset) {}
