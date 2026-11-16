package io.github.burogurama.dexpp;

import java.util.Objects;
import java.util.Optional;
import java.util.OptionalLong;

/**
 * The value of a resource in one configuration, as the table stores it. A value that refers to
 * another resource is not followed. {@link ResourceTable#values} returns them.
 *
 * @param configuration the configuration, written like the qualifiers of a {@code res/} folder
 *     name: {@code ""} for the default configuration ({@code res/values}), {@code "v31"} for
 *     Android 12 and newer, {@code "fr"} for French, {@code "b+sr+Latn-land-night-v8"} for a
 *     combination. The text is the one {@code aapt2 dump resources} prints.
 * @param type the value's type, a {@code Res_value} data type from Android's {@code
 *     ResourceTypes.h}: for example {@code 0x01} a reference to another resource, {@code 0x03} a
 *     string, {@code 0x10} a decimal integer, {@code 0x11} a hexadecimal integer, {@code 0x12} a
 *     boolean
 * @param data the raw 32-bit data: the id of the resource a reference names, the integer itself, 0
 *     or not 0 for a boolean (aapt2 writes true as {@code 0xFFFFFFFF}), or the index of a string in
 *     the table's string pool
 * @param string the text of a string value (type {@code 0x03}), or empty for any other type
 * @param asBool the value read as a boolean, the way Android reads a boolean attribute: an
 *     integer-typed value is true when it is not 0; a string is true when it is exactly {@code
 *     "1"}, {@code "true"} or {@code "TRUE"}, and false otherwise; a floating-point value, a
 *     dimension or a fraction is false. Empty for a reference (not followed here), a null value or
 *     an empty string, for which Android uses the attribute's default.
 * @param asInt the value read as an integer: any integer-typed value (an integer, a boolean, a
 *     color), as its signed 32-bit data. Empty for any other type.
 */
public record ResourceValue(
        String configuration,
        int type,
        int data,
        Optional<String> string,
        Optional<Boolean> asBool,
        OptionalLong asInt) {
    /** Checks the parts. */
    public ResourceValue {
        Objects.requireNonNull(configuration, "configuration");
        Objects.requireNonNull(string, "string");
        Objects.requireNonNull(asBool, "asBool");
        Objects.requireNonNull(asInt, "asInt");
    }
}
