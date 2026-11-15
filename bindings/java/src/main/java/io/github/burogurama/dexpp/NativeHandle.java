package io.github.burogurama.dexpp;

import java.lang.foreign.MemorySegment;
import java.lang.ref.Cleaner;
import java.lang.ref.Reference;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.ToIntFunction;
import java.util.function.ToLongFunction;

/**
 * Owns one C handle (a {@code DexppClass *}, a {@code DexppModelDex *}, ...) for a public handle
 * class.
 *
 * <p>{@link #close()} frees the C handle at once. A {@link Cleaner} frees it when this object
 * becomes unreachable without being closed. The public class holds its {@code NativeHandle} in a
 * field, so the two die together.
 *
 * <p>Every use of the C pointer goes through {@link #call} (or one of its variants). It keeps this
 * object reachable until the C call returns, with {@link Reference#reachabilityFence}. Without
 * that, the garbage collector could find the object unreachable while C still uses the pointer, and
 * the cleaner would free the C handle in the middle of the call.
 */
final class NativeHandle {
    private static final Cleaner CLEANER = Cleaner.create();

    private final String type;
    private volatile MemorySegment address;
    private final Cleaner.Cleanable cleanable;

    /**
     * Takes ownership of a C handle.
     *
     * @param type the public class name, for error messages
     * @param address the C handle; not NULL
     * @param free the C function that frees it
     */
    NativeHandle(String type, MemorySegment address, Consumer<MemorySegment> free) {
        this.type = type;
        this.address = address;
        this.cleanable = CLEANER.register(this, new Free(address, free));
    }

    /** The cleaning action. It must not refer to the NativeHandle. */
    private record Free(MemorySegment address, Consumer<MemorySegment> free) implements Runnable {
        @Override
        public void run() {
            free.accept(address);
        }
    }

    private MemorySegment address() {
        MemorySegment a = address;
        if (a == null) {
            throw new IllegalStateException("dexpp: " + type + " used after close()");
        }
        return a;
    }

    /** Runs {@code body} with the C pointer and returns its result. */
    <T> T call(Function<MemorySegment, T> body) {
        try {
            return body.apply(address());
        } finally {
            Reference.reachabilityFence(this);
        }
    }

    int callInt(ToIntFunction<MemorySegment> body) {
        try {
            return body.applyAsInt(address());
        } finally {
            Reference.reachabilityFence(this);
        }
    }

    long callLong(ToLongFunction<MemorySegment> body) {
        try {
            return body.applyAsLong(address());
        } finally {
            Reference.reachabilityFence(this);
        }
    }

    boolean callBoolean(Predicate<MemorySegment> body) {
        try {
            return body.test(address());
        } finally {
            Reference.reachabilityFence(this);
        }
    }

    void run(Consumer<MemorySegment> body) {
        try {
            body.accept(address());
        } finally {
            Reference.reachabilityFence(this);
        }
    }

    /** Frees the C handle. A second call does nothing. */
    void close() {
        address = null;
        cleanable.clean();
    }
}
