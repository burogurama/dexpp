package io.github.burogurama.dexpp;

import java.util.List;

/**
 * A run of instructions that is always entered at its first instruction and left at its last one.
 *
 * @param index the position of the block in {@link Cfg#blocks()}
 * @param startOffset the offset of the block's first instruction, in code units
 * @param endOffset the offset just past the block's last instruction
 * @param firstInsn the index in {@link DexMethod#instructions()} of the block's first instruction
 * @param insnCount the number of instructions in the block
 * @param successors the edges that leave the block; none when it returns or throws
 * @param predecessors the indices in {@link Cfg#blocks()} of the blocks with an edge to this one
 * @param isHandlerEntry true when the block is the start of a catch handler
 */
public record BasicBlock(
        int index,
        int startOffset,
        int endOffset,
        int firstInsn,
        int insnCount,
        List<CfgEdge> successors,
        List<Integer> predecessors,
        boolean isHandlerEntry) {
    /** Copies the lists into immutable lists. */
    public BasicBlock {
        successors = List.copyOf(successors);
        predecessors = List.copyOf(predecessors);
    }
}
