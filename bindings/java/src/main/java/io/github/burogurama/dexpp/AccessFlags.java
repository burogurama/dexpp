package io.github.burogurama.dexpp;

import java.util.ArrayList;
import java.util.List;

/**
 * The access flags of a class, a method or a field, as the DEX format stores them. The constants
 * name each flag:
 *
 * {@snippet :
 * AccessFlags flags = method.accessFlags();
 * if (flags.has(AccessFlags.STATIC)) {
 *     // ...
 * }
 * }
 *
 * <p>Two pairs of flags share a bit, because the DEX format gives them the same value: {@link
 * #VOLATILE} and {@link #BRIDGE} (0x40), and {@link #TRANSIENT} and {@link #VARARGS} (0x80). The
 * first of each pair is for fields, the second for methods. So {@code VOLATILE.equals(BRIDGE)} is
 * true.
 *
 * @param value the flags as one number, the way the DEX file stores them
 */
public record AccessFlags(int value) {
    /** No flag. */
    public static final AccessFlags NONE = new AccessFlags(0);

    /** {@code public}. */
    public static final AccessFlags PUBLIC = new AccessFlags(0x0001);

    /** {@code private}. */
    public static final AccessFlags PRIVATE = new AccessFlags(0x0002);

    /** {@code protected}. */
    public static final AccessFlags PROTECTED = new AccessFlags(0x0004);

    /** {@code static}. */
    public static final AccessFlags STATIC = new AccessFlags(0x0008);

    /** {@code final}. */
    public static final AccessFlags FINAL = new AccessFlags(0x0010);

    /** {@code synchronized}, on a method. */
    public static final AccessFlags SYNCHRONIZED = new AccessFlags(0x0020);

    /** {@code volatile}, on a field. The same bit as {@link #BRIDGE}. */
    public static final AccessFlags VOLATILE = new AccessFlags(0x0040);

    /** A bridge method that the compiler adds. The same bit as {@link #VOLATILE}. */
    public static final AccessFlags BRIDGE = new AccessFlags(0x0040);

    /** {@code transient}, on a field. The same bit as {@link #VARARGS}. */
    public static final AccessFlags TRANSIENT = new AccessFlags(0x0080);

    /** A method with a variable number of arguments. The same bit as {@link #TRANSIENT}. */
    public static final AccessFlags VARARGS = new AccessFlags(0x0080);

    /** {@code native}: the method's code is not in the DEX file. */
    public static final AccessFlags NATIVE = new AccessFlags(0x0100);

    /** An interface. */
    public static final AccessFlags INTERFACE = new AccessFlags(0x0200);

    /** {@code abstract}. */
    public static final AccessFlags ABSTRACT = new AccessFlags(0x0400);

    /** {@code strictfp}. */
    public static final AccessFlags STRICT = new AccessFlags(0x0800);

    /** Made by the compiler, not written in the source. */
    public static final AccessFlags SYNTHETIC = new AccessFlags(0x1000);

    /** An annotation type. */
    public static final AccessFlags ANNOTATION = new AccessFlags(0x2000);

    /** An enum, or an enum constant. */
    public static final AccessFlags ENUM = new AccessFlags(0x4000);

    /** A constructor or a static initializer. */
    public static final AccessFlags CONSTRUCTOR = new AccessFlags(0x10000);

    /** A method declared {@code synchronized} in the source. */
    public static final AccessFlags DECLARED_SYNCHRONIZED = new AccessFlags(0x20000);

    private static final AccessFlags[] NAMED = {
        PUBLIC,
        PRIVATE,
        PROTECTED,
        STATIC,
        FINAL,
        SYNCHRONIZED,
        VOLATILE,
        TRANSIENT,
        NATIVE,
        INTERFACE,
        ABSTRACT,
        STRICT,
        SYNTHETIC,
        ANNOTATION,
        ENUM,
        CONSTRUCTOR,
        DECLARED_SYNCHRONIZED,
    };
    private static final String[] NAMES = {
        "public", "private", "protected", "static", "final", "synchronized", "volatile/bridge",
        "transient/varargs", "native", "interface", "abstract", "strict", "synthetic", "annotation",
        "enum", "constructor", "declared-synchronized",
    };

    /**
     * Combines flags into one value.
     *
     * @param flags the flags to set
     * @return a value with every bit of {@code flags} set
     */
    public static AccessFlags of(AccessFlags... flags) {
        int value = 0;
        for (AccessFlags f : flags) {
            value |= f.value;
        }
        return new AccessFlags(value);
    }

    /**
     * Tells whether every bit of {@code flags} is set.
     *
     * @param flags one flag, or several combined with {@link #of} or {@link #with}
     * @return true when this value has all of them
     */
    public boolean has(AccessFlags flags) {
        return (value & flags.value) == flags.value;
    }

    /**
     * Adds flags.
     *
     * @param flags the flags to add
     * @return a value with the bits of this value and of {@code flags}
     */
    public AccessFlags with(AccessFlags flags) {
        return new AccessFlags(value | flags.value);
    }

    /**
     * Lists the flags that are set, such as {@code "public|static"}. A bit that two flags share is
     * written with both names, such as {@code "volatile/bridge"}. A bit no flag uses is written in
     * hex. No flag at all gives {@code "0"}.
     */
    @Override
    public String toString() {
        List<String> names = new ArrayList<>();
        int rest = value;
        for (int i = 0; i < NAMED.length; i++) {
            if ((value & NAMED[i].value) != 0) {
                names.add(NAMES[i]);
                rest &= ~NAMED[i].value;
            }
        }
        if (rest != 0) {
            names.add("0x" + Integer.toHexString(rest));
        }
        return names.isEmpty() ? "0" : String.join("|", names);
    }
}
