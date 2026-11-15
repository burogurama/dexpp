package io.github.burogurama.dexpp;

import java.lang.foreign.MemorySegment;
import java.util.List;

/**
 * A method defined by a class in a loaded DEX file.
 *
 * <p>It is a handle: {@link #close()} frees it, and a {@link java.lang.ref.Cleaner} frees it when
 * it is no longer used. It keeps the analysis alive, so it stays usable after its {@link DexClass}
 * and its {@link AnalysisContext} are closed.
 */
public final class DexMethod implements AutoCloseable {
    private final NativeHandle handle;

    DexMethod(MemorySegment pointer) {
        handle = new NativeHandle("DexMethod", pointer, Native::dexpp_method_free);
    }

    /** Frees the method handle at once. A second call does nothing. */
    @Override
    public void close() {
        handle.close();
    }

    /**
     * The method name. A constructor is {@code "<init>"}, and the static initializer is {@code
     * "<clinit>"}.
     *
     * @return the name
     */
    public String name() {
        return handle.call(p -> Mem.takeString(Native.dexpp_method_name(p)));
    }

    /**
     * The prototype: the parameter types inside parentheses, then the return type, such as {@code
     * "(Ljava/lang/String;I)V"}. It is the {@link MethodId#proto()} of {@link #id()}.
     *
     * @return the prototype
     */
    public String proto() {
        return handle.call(p -> Mem.takeString(Native.dexpp_method_proto(p)));
    }

    /**
     * The class that defines the method.
     *
     * @return a reference to the class
     */
    public ClassRef declaringClass() {
        return handle.call(p -> new ClassRef(Native.dexpp_method_declaring_class(p)));
    }

    /**
     * The method's identity: its class, name and prototype. It is the identity that the call graph
     * and the cross-references use.
     *
     * @return the identity
     */
    public MethodId id() {
        try (ClassRef cls = declaringClass()) {
            return new MethodId(cls.descriptor(), name(), proto());
        }
    }

    /**
     * The descriptor of the return type; {@code "V"} means void.
     *
     * @return the return type
     */
    public String returnType() {
        return handle.call(p -> Mem.takeString(Native.dexpp_method_return_type(p)));
    }

    /**
     * The descriptors of the declared parameter types, in order. The implicit {@code this} is not
     * included.
     *
     * @return the parameter types
     */
    public List<String> parameters() {
        return handle.call(
                p -> Mem.takeStrings((arena, count) -> Native.dexpp_method_parameters(p, count)));
    }

    /**
     * The method's access flags.
     *
     * @return the flags
     */
    public AccessFlags accessFlags() {
        return new AccessFlags(handle.callInt(Native::dexpp_method_access_flags));
    }

    /**
     * Tells whether the method has bytecode. It is false for abstract and native methods.
     *
     * @return true when the method has code
     */
    public boolean hasCode() {
        return handle.callBoolean(p -> Native.dexpp_method_has_code(p) != 0);
    }

    /**
     * Tells whether the method is static.
     *
     * @return true for a static method
     */
    public boolean isStatic() {
        return handle.callBoolean(p -> Native.dexpp_method_is_static(p) != 0);
    }

    /**
     * Tells whether the method is {@code "<init>"} or {@code "<clinit>"}.
     *
     * @return true for a constructor or the static initializer
     */
    public boolean isConstructor() {
        return handle.callBoolean(p -> Native.dexpp_method_is_constructor(p) != 0);
    }

    /**
     * Tells whether the method is public.
     *
     * @return true for a public method
     */
    public boolean isPublic() {
        return accessFlags().has(AccessFlags.PUBLIC);
    }

    /**
     * Tells whether the method is private.
     *
     * @return true for a private method
     */
    public boolean isPrivate() {
        return accessFlags().has(AccessFlags.PRIVATE);
    }

    /**
     * Tells whether the method is abstract.
     *
     * @return true for an abstract method
     */
    public boolean isAbstract() {
        return accessFlags().has(AccessFlags.ABSTRACT);
    }

    /**
     * Tells whether the method is native: its code is not in the DEX file.
     *
     * @return true for a native method
     */
    public boolean isNative() {
        return accessFlags().has(AccessFlags.NATIVE);
    }

    /**
     * The annotations on the method.
     *
     * @return the annotations
     */
    public List<Annotation> annotations() {
        return handle.call(p ->
                Values.takeAnnotations(
                        (arena, count) -> Native.dexpp_method_annotations(p, count)));
    }

    // ---- code ---------------------------------------------------------------------

    /**
     * The decoded bytecode of the method, in order. The library decodes the bytecode once and keeps
     * it; each call copies it into new records.
     *
     * @return the instructions; empty for a method without code
     */
    public List<Instruction> instructions() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_instructions(p, count),
                        Layouts.Instruction.LAYOUT,
                        Code::instruction,
                        Native::dexpp_instruction_array_free));
    }

    /**
     * The method's control-flow graph. The library builds it once and keeps it; each call copies it
     * into a new {@link Cfg}.
     *
     * @return the graph; empty for a method without code
     */
    public Cfg cfg() {
        return new Cfg(
                handle.call(p ->
                        Mem.take(
                                (arena, count) -> Native.dexpp_method_cfg(p, count),
                                Layouts.BasicBlock.LAYOUT,
                                Code::basicBlock,
                                Native::dexpp_basic_block_array_free)));
    }

    /**
     * The try ranges of the method and their handlers.
     *
     * @return the try blocks; empty when the method has none
     */
    public List<TryBlock> tryBlocks() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_try_blocks(p, count),
                        Layouts.TryBlock.LAYOUT,
                        Code::tryBlock,
                        Native::dexpp_try_block_array_free));
    }

    /**
     * The number of registers in the method's frame.
     *
     * @return the register count; 0 for a method without code
     */
    public int registerCount() {
        return handle.callInt(p -> Short.toUnsignedInt(Native.dexpp_method_register_count(p)));
    }

    /**
     * The incoming arguments and their registers, with {@code this} first for an instance method,
     * then the declared parameters in order.
     *
     * @return the parameter registers; empty for a method without code
     */
    public List<ParameterRegister> parameterRegisters() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_parameter_registers(p, count),
                        Layouts.ParameterRegister.LAYOUT,
                        (m, b) ->
                                new ParameterRegister(
                                        Mem.u16(m, b + Layouts.ParameterRegister.REG),
                                        Mem.string(m, b + Layouts.ParameterRegister.TYPE),
                                        Mem.bool(m, b + Layouts.ParameterRegister.IS_THIS),
                                        Mem.bool(m, b + Layouts.ParameterRegister.IS_WIDE)),
                        Native::dexpp_parameter_register_array_free));
    }

    /**
     * Every call in the method's code, in order. The targets are as the bytecode names them:
     * inheritance and virtual dispatch are not applied. The {@link CallGraph} applies them.
     *
     * @return the calls
     */
    public List<MethodCall> calls() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_calls(p, count),
                        Layouts.MethodCall.LAYOUT,
                        (m, b) ->
                                new MethodCall(
                                        Mem.i32(m, b + Layouts.MethodCall.CODE_OFFSET),
                                        Mem.methodId(m, b + Layouts.MethodCall.TARGET),
                                        InvokeKind.of(Mem.i32(m, b + Layouts.MethodCall.KIND))),
                        Native::dexpp_method_call_array_free));
    }

    /**
     * Every field read and write in the method's code, in order.
     *
     * @return the field accesses
     */
    public List<FieldAccess> fieldAccesses() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_field_accesses(p, count),
                        Layouts.FieldAccess.LAYOUT,
                        (m, b) ->
                                new FieldAccess(
                                        Mem.i32(m, b + Layouts.FieldAccess.CODE_OFFSET),
                                        Mem.fieldId(m, b + Layouts.FieldAccess.FIELD),
                                        Mem.bool(m, b + Layouts.FieldAccess.IS_WRITE),
                                        Mem.bool(m, b + Layouts.FieldAccess.IS_STATIC)),
                        Native::dexpp_field_access_array_free));
    }

    /**
     * Every {@code const-string} in the method's code, in order.
     *
     * @return the string loads
     */
    public List<StringLoad> stringLoads() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_string_loads(p, count),
                        Layouts.StringLoad.LAYOUT,
                        (m, b) ->
                                new StringLoad(
                                        Mem.i32(m, b + Layouts.StringLoad.CODE_OFFSET),
                                        Mem.text(m, b + Layouts.StringLoad.VALUE)),
                        Native::dexpp_string_load_array_free));
    }

    /**
     * Every instruction in the method's code that names a type, in order.
     *
     * @return the type uses
     */
    public List<TypeUse> typeUses() {
        return handle.call(p ->
                Mem.take(
                        (arena, count) -> Native.dexpp_method_type_uses(p, count),
                        Layouts.TypeUse.LAYOUT,
                        (m, b) ->
                                new TypeUse(
                                        Mem.i32(m, b + Layouts.TypeUse.CODE_OFFSET),
                                        Mem.string(m, b + Layouts.TypeUse.DESCRIPTOR),
                                        TypeUseKind.of(Mem.i32(m, b + Layouts.TypeUse.KIND))),
                        Native::dexpp_type_use_array_free));
    }

    /** The identity, such as {@code "LFoo;->run()V"}; "closed" after {@link #close()}. */
    @Override
    public String toString() {
        try {
            return id().toString();
        } catch (IllegalStateException closed) {
            return "DexMethod (closed)";
        }
    }
}
