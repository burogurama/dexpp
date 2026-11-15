package io.github.burogurama.dexpp;

/**
 * One case of a {@link SwitchInstruction}.
 *
 * @param key the value the case matches
 * @param target where the case jumps, in code units, counted from the switch instruction's own
 *     offset
 */
public record SwitchCase(int key, int target) {}
