package io.github.burogurama.dexpp;

import java.util.List;
import java.util.Optional;

/**
 * The control-flow graph of one method. The blocks are ordered by offset, and block 0 is where the
 * method starts. It is a copy, so it stays valid on its own.
 *
 * @param blocks the basic blocks, by offset; empty for a method without code
 */
public record Cfg(List<BasicBlock> blocks) {
    /** Copies the blocks into an immutable list. */
    public Cfg {
        blocks = List.copyOf(blocks);
    }

    /**
     * The block where the method starts.
     *
     * @return block 0, or empty when the graph is empty
     */
    public Optional<BasicBlock> entry() {
        return blocks.isEmpty() ? Optional.empty() : Optional.of(blocks.get(0));
    }

    /**
     * The block that starts at this offset. Use it to follow a branch target to its block.
     *
     * @param offset an offset in code units
     * @return the block, or empty when no block starts there
     */
    public Optional<BasicBlock> blockAtOffset(int offset) {
        int lo = 0;
        int hi = blocks.size();
        while (lo < hi) { // the blocks are sorted by start offset
            int mid = (lo + hi) >>> 1;
            if (blocks.get(mid).startOffset() < offset) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < blocks.size() && blocks.get(lo).startOffset() == offset) {
            return Optional.of(blocks.get(lo));
        }
        return Optional.empty();
    }

    /**
     * Tells whether the graph has no blocks: the method is abstract or native.
     *
     * @return true for an empty graph
     */
    public boolean isEmpty() {
        return blocks.isEmpty();
    }

    /**
     * The number of blocks.
     *
     * @return the number of blocks
     */
    public int size() {
        return blocks.size();
    }
}
