#pragma once

#include <cstdint>

namespace dex {

/** What a method handle (a java.lang.invoke.MethodHandle constant) does, per
 *  the method_handle_type codes of the DEX format.  The four field kinds read
 *  or write a field, named by a FieldId; the five invoke kinds call a method,
 *  named by a MethodId.  The analysis layer (AnnotationValue::MethodHandleValue)
 *  and the editable model (model::MethodHandle) share it. */
enum class MethodHandleKind : uint8_t {
    StaticPut = 0x00,         ///< Writes a static field.
    StaticGet = 0x01,         ///< Reads a static field.
    InstancePut = 0x02,       ///< Writes an instance field.
    InstanceGet = 0x03,       ///< Reads an instance field.
    InvokeStatic = 0x04,      ///< Calls a static method.
    InvokeInstance = 0x05,    ///< Calls an instance method, with virtual dispatch.
    InvokeConstructor = 0x06, ///< Creates an object with a constructor.
    InvokeDirect = 0x07,      ///< Calls an instance method without dispatch (private, super).
    InvokeInterface = 0x08,   ///< Calls an interface method.
};

} // namespace dex
