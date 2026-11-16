package io.github.burogurama.dexpp;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_DOUBLE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.MemoryLayout;
import java.lang.foreign.StructLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * The memory layouts of the structs of the C API (dexpp.h and dexpp_model.h).
 *
 * <p>Each nested class describes one struct. Its constants are the byte offsets of the members, and
 * {@code LAYOUT} is the whole struct. The members are listed in the order of the header, with the
 * header's names, and the {@link Builder} puts padding where a C compiler puts it: every member at
 * a multiple of its alignment, and the struct size a multiple of its largest alignment. This rule
 * is the same on every 64-bit platform the library supports (Linux, macOS and Windows; x86-64 and
 * arm64). C {@code int} and the enums are 4 bytes; {@code size_t} and pointers are 8 bytes.
 *
 * <p>A test reads the struct definitions of the headers and checks that every layout here has the
 * same members, in the same order, with the same sizes.
 */
final class Layouts {
    private Layouts() {}

    /**
     * Every struct layout, by C struct name. The header test reads it. The nested classes can be
     * initialized on several threads at once, so the map is a concurrent one.
     */
    private static final Map<String, StructLayout> ALL = new ConcurrentHashMap<>();

    /** Builds a struct layout member by member, with C padding. */
    static final class Builder {
        private final List<MemoryLayout> members = new ArrayList<>();
        private long size;
        private long alignment = 1;

        /** Adds a member and returns its offset. */
        long add(String name, MemoryLayout layout) {
            long align = layout.byteAlignment();
            long padding = (align - size % align) % align;
            if (padding > 0) {
                members.add(MemoryLayout.paddingLayout(padding));
                size += padding;
            }
            long offset = size;
            members.add(layout.withName(name));
            size += layout.byteSize();
            alignment = Math.max(alignment, align);
            return offset;
        }

        long ptr(String name) {
            return add(name, ADDRESS);
        }

        long size(String name) {
            return add(name, JAVA_LONG);
        }

        long i8(String name) {
            return add(name, JAVA_BYTE);
        }

        long i16(String name) {
            return add(name, JAVA_SHORT);
        }

        /** int, int32_t, uint32_t and every enum. */
        long i32(String name) {
            return add(name, JAVA_INT);
        }

        long i64(String name) {
            return add(name, JAVA_LONG);
        }

        long f64(String name) {
            return add(name, JAVA_DOUBLE);
        }

        /** Ends the struct: adds tail padding and registers the layout. */
        StructLayout build(String name) {
            long padding = (alignment - size % alignment) % alignment;
            if (padding > 0) {
                members.add(MemoryLayout.paddingLayout(padding));
            }
            StructLayout layout =
                    MemoryLayout.structLayout(members.toArray(MemoryLayout[]::new)).withName(name);
            ALL.put(name, layout);
            return layout;
        }
    }

    // ---- dexpp.h: memory and identities ------------------------------------

    static final class Text {
        private static final Builder B = new Builder();
        static final long DATA = B.ptr("data");
        static final long LENGTH = B.size("length");
        static final StructLayout LAYOUT = B.build("DexppText");
    }

    static final class MethodId {
        private static final Builder B = new Builder();
        static final long CLASS_DESCRIPTOR = B.ptr("class_descriptor");
        static final long NAME = B.ptr("name");
        static final long PROTO = B.ptr("proto");
        static final StructLayout LAYOUT = B.build("DexppMethodId");
    }

    static final class FieldId {
        private static final Builder B = new Builder();
        static final long CLASS_DESCRIPTOR = B.ptr("class_descriptor");
        static final long NAME = B.ptr("name");
        static final long TYPE = B.ptr("type");
        static final StructLayout LAYOUT = B.build("DexppFieldId");
    }

    // ---- annotations and values --------------------------------------------

    static final class Value {
        private static final Builder B = new Builder();
        static final long KIND = B.i32("kind");
        static final long BOOL_VALUE = B.i32("bool_value");
        static final long INT_VALUE = B.i64("int_value");
        static final long FLOAT_VALUE = B.f64("float_value");
        static final long STRING_VALUE = B.add("string_value", Text.LAYOUT);
        static final long TYPE_DESCRIPTOR = B.ptr("type_descriptor");
        static final long PROTO = B.ptr("proto");
        static final long FIELD = B.add("field", FieldId.LAYOUT);
        static final long METHOD = B.add("method", MethodId.LAYOUT);
        static final long METHOD_HANDLE_KIND = B.i32("method_handle_kind");
        static final long ELEMENTS = B.ptr("elements");
        static final long ELEMENT_COUNT = B.size("element_count");
        static final long ANNOTATION = B.ptr("annotation");
        static final StructLayout LAYOUT = B.build("DexppValue");
    }

    static final class AnnotationElement {
        private static final Builder B = new Builder();
        static final long NAME = B.ptr("name");
        static final long VALUE = B.add("value", Value.LAYOUT);
        static final StructLayout LAYOUT = B.build("DexppAnnotationElement");
    }

    static final class Annotation {
        private static final Builder B = new Builder();
        static final long TYPE_DESCRIPTOR = B.ptr("type_descriptor");
        static final long VISIBILITY = B.i32("visibility");
        static final long ELEMENTS = B.ptr("elements");
        static final long ELEMENT_COUNT = B.size("element_count");
        static final StructLayout LAYOUT = B.build("DexppAnnotation");
    }

    // ---- methods -------------------------------------------------------------

    static final class ParameterRegister {
        private static final Builder B = new Builder();
        static final long REG = B.i16("reg");
        static final long TYPE = B.ptr("type");
        static final long IS_THIS = B.i32("is_this");
        static final long IS_WIDE = B.i32("is_wide");
        static final StructLayout LAYOUT = B.build("DexppParameterRegister");
    }

    static final class MethodCall {
        private static final Builder B = new Builder();
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long TARGET = B.add("target", MethodId.LAYOUT);
        static final long KIND = B.i32("kind");
        static final StructLayout LAYOUT = B.build("DexppMethodCall");
    }

    static final class FieldAccess {
        private static final Builder B = new Builder();
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long FIELD = B.add("field", FieldId.LAYOUT);
        static final long IS_WRITE = B.i32("is_write");
        static final long IS_STATIC = B.i32("is_static");
        static final StructLayout LAYOUT = B.build("DexppFieldAccess");
    }

    static final class StringLoad {
        private static final Builder B = new Builder();
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long VALUE = B.add("value", Text.LAYOUT);
        static final StructLayout LAYOUT = B.build("DexppStringLoad");
    }

    static final class DisassemblyLine {
        private static final Builder B = new Builder();
        static final long OFFSET = B.i32("offset");
        static final long TEXT = B.ptr("text");
        static final StructLayout LAYOUT = B.build("DexppDisassemblyLine");
    }

    static final class TypeUse {
        private static final Builder B = new Builder();
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long DESCRIPTOR = B.ptr("descriptor");
        static final long KIND = B.i32("kind");
        static final StructLayout LAYOUT = B.build("DexppTypeUse");
    }

    static final class CatchHandler {
        private static final Builder B = new Builder();
        static final long TYPE_DESCRIPTOR = B.ptr("type_descriptor");
        static final long HANDLER_OFFSET = B.i32("handler_offset");
        static final StructLayout LAYOUT = B.build("DexppCatchHandler");
    }

    static final class TryBlock {
        private static final Builder B = new Builder();
        static final long START_OFFSET = B.i32("start_offset");
        static final long END_OFFSET = B.i32("end_offset");
        static final long HANDLERS = B.ptr("handlers");
        static final long HANDLER_COUNT = B.size("handler_count");
        static final long HAS_CATCH_ALL = B.i32("has_catch_all");
        static final long CATCH_ALL_OFFSET = B.i32("catch_all_offset");
        static final StructLayout LAYOUT = B.build("DexppTryBlock");
    }

    // ---- instructions --------------------------------------------------------

    static final class SwitchCase {
        private static final Builder B = new Builder();
        static final long KEY = B.i32("key");
        static final long TARGET = B.i32("target");
        static final StructLayout LAYOUT = B.build("DexppSwitchCase");
    }

    static final class Instruction {
        private static final Builder B = new Builder();
        static final long KIND = B.i32("kind");
        static final long OFFSET = B.i32("offset");
        static final long OPCODE = B.i8("opcode");
        static final long SIZE = B.i8("size");
        static final long BRANCH_OFFSET = B.i32("branch_offset");
        static final long PAYLOAD_OFFSET = B.i32("payload_offset");
        static final long REG = B.i8("reg");
        static final long HAS_REG = B.i32("has_reg");
        static final long REG_A = B.i8("reg_a");
        static final long REG_B = B.i8("reg_b");
        static final long DEST = B.i16("dest");
        static final long SRC = B.i16("src");
        static final long HAS_SRC = B.i32("has_src");
        static final long SRC_A = B.i8("src_a");
        static final long SRC_B = B.i8("src_b");
        static final long VALUE = B.i64("value");
        static final long LITERAL = B.i32("literal");
        static final long STRING_INDEX = B.i32("string_index");
        static final long TYPE_INDEX = B.i32("type_index");
        static final long METHOD_HANDLE_INDEX = B.i32("method_handle_index");
        static final long PROTO_INDEX = B.i32("proto_index");
        static final long FIELD_INDEX = B.i32("field_index");
        static final long METHOD_INDEX = B.i32("method_index");
        static final long CALL_SITE_INDEX = B.i32("call_site_index");
        static final long VALUE_REG = B.i8("value_reg");
        static final long ARRAY_REG = B.i8("array_reg");
        static final long INDEX_REG = B.i8("index_reg");
        static final long OBJECT_REG = B.i8("object_reg");
        static final long HAS_OBJECT_REG = B.i32("has_object_reg");
        static final long IS_PACKED = B.i32("is_packed");
        static final long IS_ENTER = B.i32("is_enter");
        static final long IS_WRITE = B.i32("is_write");
        static final long IS_STATIC = B.i32("is_static");
        static final long WIDTH = B.i32("width");
        static final long SRC_WIDTH = B.i32("src_width");
        static final long DEST_WIDTH = B.i32("dest_width");
        static final long CASES = B.ptr("cases");
        static final long CASE_COUNT = B.size("case_count");
        static final long ARG_REGS = B.ptr("arg_regs");
        static final long ARG_COUNT = B.size("arg_count");
        static final StructLayout LAYOUT = B.build("DexppInstruction");
    }

    // ---- control-flow graph --------------------------------------------------

    static final class CfgEdge {
        private static final Builder B = new Builder();
        static final long TARGET_BLOCK = B.i32("target_block");
        static final long KIND = B.i32("kind");
        static final long CASE_KEY = B.i32("case_key");
        static final long CATCH_TYPE = B.ptr("catch_type");
        static final StructLayout LAYOUT = B.build("DexppCfgEdge");
    }

    static final class BasicBlock {
        private static final Builder B = new Builder();
        static final long INDEX = B.i32("index");
        static final long START_OFFSET = B.i32("start_offset");
        static final long END_OFFSET = B.i32("end_offset");
        static final long FIRST_INSN = B.i32("first_insn");
        static final long INSN_COUNT = B.i32("insn_count");
        static final long SUCCESSORS = B.ptr("successors");
        static final long SUCCESSOR_COUNT = B.size("successor_count");
        static final long PREDECESSORS = B.ptr("predecessors");
        static final long PREDECESSOR_COUNT = B.size("predecessor_count");
        static final long IS_HANDLER_ENTRY = B.i32("is_handler_entry");
        static final StructLayout LAYOUT = B.build("DexppBasicBlock");
    }

    // ---- call graph ----------------------------------------------------------

    static final class CallNode {
        private static final Builder B = new Builder();
        static final long INDEX = B.i32("index");
        static final long ID = B.add("id", MethodId.LAYOUT);
        static final long KIND = B.i32("kind");
        static final StructLayout LAYOUT = B.build("DexppCallNode");
    }

    static final class CallEdge {
        private static final Builder B = new Builder();
        static final long CALLER_NODE = B.i32("caller_node");
        static final long CALLEE_NODE = B.i32("callee_node");
        static final long KIND = B.i32("kind");
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long ORIGIN = B.i32("origin");
        static final StructLayout LAYOUT = B.build("DexppCallEdge");
    }

    // ---- cross-references ----------------------------------------------------

    static final class MethodXref {
        private static final Builder B = new Builder();
        static final long REFERRER = B.i32("referrer");
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long KIND = B.i32("kind");
        static final StructLayout LAYOUT = B.build("DexppMethodXref");
    }

    static final class FieldXref {
        private static final Builder B = new Builder();
        static final long REFERRER = B.i32("referrer");
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long IS_WRITE = B.i32("is_write");
        static final long IS_STATIC = B.i32("is_static");
        static final StructLayout LAYOUT = B.build("DexppFieldXref");
    }

    static final class StringXref {
        private static final Builder B = new Builder();
        static final long REFERRER = B.i32("referrer");
        static final long CODE_OFFSET = B.i32("code_offset");
        static final StructLayout LAYOUT = B.build("DexppStringXref");
    }

    static final class TypeXref {
        private static final Builder B = new Builder();
        static final long REFERRER = B.i32("referrer");
        static final long CODE_OFFSET = B.i32("code_offset");
        static final long KIND = B.i32("kind");
        static final StructLayout LAYOUT = B.build("DexppTypeXref");
    }

    static final class MethodRefGroup {
        private static final Builder B = new Builder();
        static final long TARGET = B.add("target", MethodId.LAYOUT);
        static final long SITES = B.ptr("sites");
        static final long SITE_COUNT = B.size("site_count");
        static final StructLayout LAYOUT = B.build("DexppMethodRefGroup");
    }

    // ---- APK -------------------------------------------------------------------

    static final class IntentFilter {
        private static final Builder B = new Builder();
        static final long ACTIONS = B.ptr("actions");
        static final long ACTION_COUNT = B.size("action_count");
        static final long CATEGORIES = B.ptr("categories");
        static final long CATEGORY_COUNT = B.size("category_count");
        static final long IS_LAUNCHER = B.i32("is_launcher");
        static final StructLayout LAYOUT = B.build("DexppIntentFilter");
    }

    static final class Component {
        private static final Builder B = new Builder();
        static final long NAME = B.ptr("name");
        static final long NAME_REFERENCE = B.i32("name_reference");
        static final long KIND = B.i32("kind");
        static final long EXPORTED = B.i32("exported");
        static final long EXPORTED_REFERENCE = B.i32("exported_reference");
        static final long EXPORTED_VARIES = B.i32("exported_varies");
        static final long PERMISSION = B.ptr("permission");
        static final long PERMISSION_REFERENCE = B.i32("permission_reference");
        static final long INTENT_FILTERS = B.ptr("intent_filters");
        static final long INTENT_FILTER_COUNT = B.size("intent_filter_count");
        static final long GUARDED_BY_SIGNATURE_PERMISSION =
                B.i32("guarded_by_signature_permission");
        static final long IS_EXPORTED = B.i32("is_exported");
        static final long IS_REACHABLE = B.i32("is_reachable");
        static final StructLayout LAYOUT = B.build("DexppComponent");
    }

    static final class DeclaredPermission {
        private static final Builder B = new Builder();
        static final long NAME = B.ptr("name");
        static final long HAS_PROTECTION_LEVEL = B.i32("has_protection_level");
        static final long PROTECTION_LEVEL = B.i32("protection_level");
        static final long PROTECTION_LEVEL_VARIES = B.i32("protection_level_varies");
        static final StructLayout LAYOUT = B.build("DexppDeclaredPermission");
    }

    static final class Manifest {
        private static final Builder B = new Builder();
        static final long PACKAGE = B.ptr("package");
        static final long HAS_VERSION_CODE = B.i32("has_version_code");
        static final long VERSION_CODE = B.i64("version_code");
        static final long VERSION_CODE_REFERENCE = B.i32("version_code_reference");
        static final long VERSION_NAME = B.ptr("version_name");
        static final long VERSION_NAME_REFERENCE = B.i32("version_name_reference");
        static final long HAS_MIN_SDK = B.i32("has_min_sdk");
        static final long MIN_SDK = B.i64("min_sdk");
        static final long MIN_SDK_REFERENCE = B.i32("min_sdk_reference");
        static final long HAS_TARGET_SDK = B.i32("has_target_sdk");
        static final long TARGET_SDK = B.i64("target_sdk");
        static final long TARGET_SDK_REFERENCE = B.i32("target_sdk_reference");
        static final long PERMISSIONS = B.ptr("permissions");
        static final long PERMISSION_COUNT = B.size("permission_count");
        static final long DECLARED_PERMISSIONS = B.ptr("declared_permissions");
        static final long DECLARED_PERMISSION_COUNT = B.size("declared_permission_count");
        static final long COMPONENTS = B.ptr("components");
        static final long COMPONENT_COUNT = B.size("component_count");
        static final long LAUNCHER_ACTIVITY = B.ptr("launcher_activity");
        static final long DEBUGGABLE = B.i32("debuggable");
        static final long DEBUGGABLE_REFERENCE = B.i32("debuggable_reference");
        static final long DEBUGGABLE_VARIES = B.i32("debuggable_varies");
        static final long ALLOW_BACKUP = B.i32("allow_backup");
        static final long ALLOW_BACKUP_REFERENCE = B.i32("allow_backup_reference");
        static final long ALLOW_BACKUP_VARIES = B.i32("allow_backup_varies");
        static final StructLayout LAYOUT = B.build("DexppManifest");
    }

    static final class ResourceName {
        private static final Builder B = new Builder();
        static final long PACKAGE = B.ptr("package");
        static final long TYPE = B.ptr("type");
        static final long ENTRY = B.ptr("entry");
        static final StructLayout LAYOUT = B.build("DexppResourceName");
    }

    static final class ResourceValue {
        private static final Builder B = new Builder();
        static final long CONFIGURATION = B.ptr("configuration");
        static final long TYPE = B.i8("type");
        static final long DATA = B.i32("data");
        static final long STRING = B.add("string", Text.LAYOUT);
        static final long BOOLEAN = B.i32("boolean");
        static final long HAS_INTEGER = B.i32("has_integer");
        static final long INTEGER = B.i64("integer");
        static final StructLayout LAYOUT = B.build("DexppResourceValue");
    }

    static final class Certificate {
        private static final Builder B = new Builder();
        static final long DER = B.ptr("der");
        static final long DER_LENGTH = B.size("der_length");
        static final long SUBJECT = B.ptr("subject");
        static final long ISSUER = B.ptr("issuer");
        static final long SERIAL_HEX = B.ptr("serial_hex");
        static final long NOT_BEFORE = B.ptr("not_before");
        static final long NOT_AFTER = B.ptr("not_after");
        static final long SHA1_HEX = B.ptr("sha1_hex");
        static final long SHA256_HEX = B.ptr("sha256_hex");
        static final long SELF_SIGNED = B.i32("self_signed");
        static final StructLayout LAYOUT = B.build("DexppCertificate");
    }

    static final class SigningInfo {
        private static final Builder B = new Builder();
        static final long V1 = B.i32("v1");
        static final long V2 = B.i32("v2");
        static final long V3 = B.i32("v3");
        static final long CERTIFICATES = B.ptr("certificates");
        static final long CERTIFICATE_COUNT = B.size("certificate_count");
        static final StructLayout LAYOUT = B.build("DexppSigningInfo");
    }

    // ---- dexpp_model.h -------------------------------------------------------

    static final class PoolStats {
        private static final Builder B = new Builder();
        static final long STRINGS = B.size("strings");
        static final long TYPES = B.size("types");
        static final long PROTOS = B.size("protos");
        static final long FIELDS = B.size("fields");
        static final long METHODS = B.size("methods");
        static final long METHOD_HANDLES = B.size("method_handles");
        static final long CALL_SITES = B.size("call_sites");
        static final StructLayout LAYOUT = B.build("DexppPoolStats");
    }

    static final class Problem {
        private static final Builder B = new Builder();
        static final long WHERE = B.ptr("where");
        static final long MESSAGE = B.ptr("message");
        static final StructLayout LAYOUT = B.build("DexppProblem");
    }

    static final class ClassInfo {
        private static final Builder B = new Builder();
        static final long DESCRIPTOR = B.ptr("descriptor");
        static final long ACCESS_FLAGS = B.i32("access_flags");
        static final long SUPERCLASS = B.ptr("superclass");
        static final long SOURCE_FILE = B.ptr("source_file");
        static final long INTERFACES = B.ptr("interfaces");
        static final long INTERFACE_COUNT = B.size("interface_count");
        static final long FIELD_COUNT = B.size("field_count");
        static final long METHOD_COUNT = B.size("method_count");
        static final StructLayout LAYOUT = B.build("DexppClassInfo");
    }

    /** Loads every nested class, so that {@link #ALL} holds every layout. */
    static Map<String, StructLayout> all() {
        StructLayout[] every = {
            Text.LAYOUT,
            MethodId.LAYOUT,
            FieldId.LAYOUT,
            Value.LAYOUT,
            AnnotationElement.LAYOUT,
            Annotation.LAYOUT,
            ParameterRegister.LAYOUT,
            MethodCall.LAYOUT,
            FieldAccess.LAYOUT,
            StringLoad.LAYOUT,
            DisassemblyLine.LAYOUT,
            TypeUse.LAYOUT,
            CatchHandler.LAYOUT,
            TryBlock.LAYOUT,
            SwitchCase.LAYOUT,
            Instruction.LAYOUT,
            CfgEdge.LAYOUT,
            BasicBlock.LAYOUT,
            CallNode.LAYOUT,
            CallEdge.LAYOUT,
            MethodXref.LAYOUT,
            FieldXref.LAYOUT,
            StringXref.LAYOUT,
            TypeXref.LAYOUT,
            MethodRefGroup.LAYOUT,
            IntentFilter.LAYOUT,
            Component.LAYOUT,
            DeclaredPermission.LAYOUT,
            Manifest.LAYOUT,
            ResourceName.LAYOUT,
            ResourceValue.LAYOUT,
            Certificate.LAYOUT,
            SigningInfo.LAYOUT,
            PoolStats.LAYOUT,
            Problem.LAYOUT,
            ClassInfo.LAYOUT,
        };
        assert every.length == ALL.size();
        return ALL;
    }
}
