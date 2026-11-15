package io.github.burogurama.dexpp;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.PaddingLayout;
import java.lang.foreign.SegmentAllocator;
import java.lang.foreign.StructLayout;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Keeps the hand-written low-level layer in step with the C headers. It reads bindings/c/dexpp.h
 * and bindings/c/dexpp_model.h, and checks that:
 *
 * <ul>
 *   <li>{@link Native} has one method per C function, with the same name and matching parameter and
 *       return types;
 *   <li>{@link Layouts} has one layout per C struct, with the same members in the same order and
 *       with the same sizes. The offsets then follow, because {@link Layouts.Builder} pads as a C
 *       compiler does.
 * </ul>
 */
class HeaderTest {
    private static String header(String name) throws IOException {
        String text =
                Files.readString(
                        TestData.ROOT.resolve("bindings/c/" + name), StandardCharsets.UTF_8);
        return text.replaceAll("(?s)/\\*.*?\\*/", " ");
    }

    private static String headers() throws IOException {
        return header("dexpp.h") + "\n" + header("dexpp_model.h");
    }

    /** The Java type the low-level layer uses for a C type. */
    private static Class<?> javaType(String c) {
        String t = c.replace("const", " ").replaceAll("\\s+", " ").trim();
        if (t.contains("*")) {
            return MemorySegment.class;
        }
        return switch (t) {
            case "void" -> void.class;
            case "int", "uint32_t", "int32_t", "DexppErrorCode" -> int.class;
            case "size_t", "int64_t" -> long.class;
            case "uint16_t" -> short.class;
            case "uint8_t" -> byte.class;
            case "DexppPoolStats" -> MemorySegment.class;
            default -> throw new AssertionError("unknown C type " + c);
        };
    }

    @Test
    void nativeHasEveryFunctionWithMatchingTypes() throws IOException {
        Pattern declaration =
                Pattern.compile("DEXPP_C_API\\s+([^;(]*?)\\b(dexpp_\\w+)\\s*\\(([^)]*)\\)\\s*;");
        Map<String, List<Class<?>>> expected = new TreeMap<>();
        Matcher m = declaration.matcher(headers());
        while (m.find()) {
            List<Class<?>> types = new ArrayList<>();
            String ret = m.group(1);
            types.add(javaType(ret));
            if (ret.trim().equals("DexppPoolStats")) {
                types.add(SegmentAllocator.class); // the allocator for a struct result
            }
            for (String param : m.group(3).split(",")) {
                String p = param.trim();
                types.add(
                        javaType(
                                p.substring(
                                        0,
                                        p.length() - p.replaceAll(".*?(\\w+)$", "$1").length())));
            }
            expected.put(m.group(2), types);
        }
        assertTrue(
                expected.size() > 150,
                "only " + expected.size() + " functions found in the headers");

        Map<String, List<Class<?>>> actual = new TreeMap<>();
        for (Method method : Native.class.getDeclaredMethods()) {
            if (Modifier.isStatic(method.getModifiers()) && method.getName().startsWith("dexpp_")) {
                List<Class<?>> types = new ArrayList<>();
                types.add(method.getReturnType());
                types.addAll(List.of(method.getParameterTypes()));
                actual.put(method.getName(), types);
            }
        }
        assertEquals(expected.keySet(), actual.keySet(), "functions");
        for (String name : expected.keySet()) {
            assertEquals(expected.get(name), actual.get(name), name);
        }
    }

    /** The byte size of a C member type, for the struct check. */
    private static long cSize(String type, Map<String, StructLayout> layouts) {
        String t = type.replace("const", " ").replace("struct", " ").replaceAll("\\s+", " ").trim();
        if (t.contains("*")) {
            return 8;
        }
        return switch (t) {
            case "size_t", "int64_t", "uint64_t", "double" -> 8;
            case "int", "uint32_t", "int32_t" -> 4;
            case "uint16_t" -> 2;
            case "uint8_t" -> 1;
            default -> {
                if (layouts.containsKey(t)) {
                    yield layouts.get(t).byteSize();
                }
                if (t.startsWith("Dexpp")) {
                    yield 4; // an enum
                }
                throw new AssertionError("unknown C type " + type);
            }
        };
    }

    @Test
    void layoutsMatchEveryStruct() throws IOException {
        Map<String, StructLayout> layouts = Layouts.all();
        Pattern struct = Pattern.compile("typedef struct (\\w+)\\s*\\{([^}]*)\\}\\s*\\1\\s*;");
        Pattern member = Pattern.compile("^(.*?[\\s*])(\\w+)$");
        Matcher m = struct.matcher(headers());
        List<String> seen = new ArrayList<>();
        while (m.find()) {
            String name = m.group(1);
            seen.add(name);
            StructLayout layout = layouts.get(name);
            assertTrue(layout != null, "no layout for " + name);
            List<String> expected = new ArrayList<>();
            for (String line : m.group(2).replaceAll("//[^\n]*", "").split(";")) {
                String decl = line.replaceAll("\\s+", " ").trim();
                if (decl.isEmpty()) {
                    continue;
                }
                Matcher parts = member.matcher(decl);
                assertTrue(parts.matches(), "cannot read member '" + decl + "' of " + name);
                expected.add(parts.group(2) + ":" + cSize(parts.group(1), layouts));
            }
            List<String> actual = new ArrayList<>();
            for (MemoryLayout element : layout.memberLayouts()) {
                if (!(element instanceof PaddingLayout)) {
                    actual.add(element.name().orElseThrow() + ":" + element.byteSize());
                }
            }
            assertEquals(expected, actual, name);
        }
        assertEquals(
                new TreeSet<>(layouts.keySet()),
                new TreeSet<>(seen),
                "structs in the headers and in Layouts");
    }
}
