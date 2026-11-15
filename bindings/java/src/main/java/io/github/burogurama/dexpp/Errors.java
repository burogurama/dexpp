package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.FileSystemException;
import java.nio.file.NoSuchFileException;
import java.util.ArrayList;
import java.util.List;

/**
 * Turns the {@code DexppError} of a failed C call into a Java exception, and frees it.
 *
 * <p>A C function that can fail takes a {@code DexppError **err}. The caller passes a slot from
 * {@link #slot}; after a failure, {@link #io} or {@link #unchecked} reads the error object out of
 * the slot.
 */
final class Errors {
    private Errors() {}

    /** A zeroed {@code DexppError *} slot to pass as {@code err}. */
    static MemorySegment slot(Arena arena) {
        return arena.allocate(ADDRESS);
    }

    /**
     * The exception for a failed call that takes file paths. A problem with a file is returned as
     * an {@link IOException} for the caller to throw; every other error is thrown here as a {@link
     * DexppException}.
     *
     * @param slot the {@code err} slot of the call
     * @param paths the paths the call took, to name the file in the exception
     */
    static IOException io(MemorySegment slot, List<String> paths) {
        Exception e = convert(slot, paths);
        if (e instanceof IOException io) {
            return io;
        }
        throw (DexppException) e;
    }

    /** The exception for a failed call that does not touch files. */
    static DexppException unchecked(MemorySegment slot) {
        Exception e = convert(slot, List.of());
        if (e instanceof IOException io) {
            // The C API reports file errors only for calls that take a path.
            throw new UncheckedIOException(io);
        }
        return (DexppException) e;
    }

    private static Exception convert(MemorySegment slot, List<String> paths) {
        MemorySegment error = slot.get(ADDRESS, 0);
        if (Mem.isNull(error)) {
            return new DexppException(
                    ErrorCode.INTERNAL, "dexpp: the call failed without an error");
        }
        try {
            int value = Native.dexpp_error_code(error);
            String message = Mem.cString(Native.dexpp_error_message(error));
            if (message == null) {
                message = "";
            }
            ErrorCode code;
            try {
                code = ErrorCode.of(value);
            } catch (IllegalArgumentException unknown) {
                return new DexppException(
                        ErrorCode.INTERNAL, "error code " + value + ": " + message);
            }
            return switch (code) {
                case FILE_NOT_FOUND, CANNOT_READ, CANNOT_WRITE ->
                        fileException(code, message, paths);
                case INVALID_APK -> new InvalidApkException(message);
                case INVALID_DEX -> new InvalidDexException(message);
                case UNSUPPORTED_DEX -> new UnsupportedDexException(message);
                case INVALID_MODEL -> new InvalidModelException(message, problems(error));
                case POOL_OVERFLOW -> new PoolOverflowException(message);
                case VERSION_TOO_LOW -> new VersionTooLowException(message);
                case MODEL -> new ModelException(message);
                case INTERNAL -> new DexppException(ErrorCode.INTERNAL, message);
            };
        } finally {
            Native.dexpp_error_free(error);
        }
    }

    private static List<Problem> problems(MemorySegment error) {
        long count = Native.dexpp_error_problem_count(error);
        List<Problem> out = new ArrayList<>();
        for (long i = 0; i < count; i++) {
            String where = Mem.cString(Native.dexpp_error_problem_where(error, i));
            String what = Mem.cString(Native.dexpp_error_problem_message(error, i));
            out.add(new Problem(where == null ? "" : where, what == null ? "" : what));
        }
        return out;
    }

    /**
     * A {@link NoSuchFileException} for {@link ErrorCode#FILE_NOT_FOUND}, and a {@link
     * FileSystemException} for the other two file codes, in the form of the JDK's own file
     * exceptions: the file, and the reason. The C message is "Cannot open file: PATH", or "Cannot
     * read file: PATH: REASON" (and the same for write), so the reason is what follows the path. A
     * message of another form is kept whole.
     */
    private static IOException fileException(ErrorCode code, String message, List<String> paths) {
        String file = null;
        String reason = message;
        for (String path : paths) {
            int end = endOfPath(message, path);
            if (end >= 0) {
                file = path;
                reason = end == message.length() ? null : message.substring(end + 2);
                break;
            }
        }
        if (file == null && paths.size() == 1) {
            file = paths.get(0);
        }
        if (code == ErrorCode.FILE_NOT_FOUND) {
            return new NoSuchFileException(file, null, reason);
        }
        return new FileSystemException(file, null, reason);
    }

    /**
     * Where ": PATH" ends in the message, when it is followed by the end of the message or by ": ";
     * otherwise -1.
     */
    private static int endOfPath(String message, String path) {
        if (path.isEmpty()) {
            return -1;
        }
        String marker = ": " + path;
        for (int at = message.indexOf(marker); at >= 0; at = message.indexOf(marker, at + 1)) {
            int end = at + marker.length();
            if (end == message.length() || message.startsWith(": ", end)) {
                return end;
            }
        }
        return -1;
    }
}
