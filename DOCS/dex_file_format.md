## File Layout

- **header** ([`header_item`](#header_item))  
  The header.


- **string_ids** ([`string_id_item[]`](#string_id_item))  
  String identifiers list. These are identifiers for all the strings used by this file, either for internal naming (e.g., type descriptors) or as constant objects referred to by code. This list must be sorted by string contents, using UTF-16 code point values (not in a locale-sensitive manner), and it must not contain any duplicate entries.


- **type_ids** ([`type_id_item[]`](#type_id_item))  
  Type identifiers list. These are identifiers for all types (classes, arrays, or primitive types) referred to by this file, whether defined in the file or not. This list must be sorted by [`string_id`](#string_id_item) index, and it must not contain any duplicate entries.


- **proto_ids** ([`proto_id_item[]`](#proto_id_item))  
  Method prototype identifiers list. These are identifiers for all prototypes referred to by this file. This list must be sorted in return-type (by [`type_id`](#type_id_item) index) major order, and then by argument list (lexicographic ordering, individual arguments ordered by [`type_id`](#type_id_item) index). The list must not contain any duplicate entries.


- **field_ids** ([`field_id_item[]`](#field_id_item))  
  Field identifiers list. These are identifiers for all fields referred to by this file, whether defined in the file or not. This list must be sorted, where the defining type (by [`type_id`](#type_id_item) index) is the major order, field name (by [`string_id`](#string_id_item) index) is the intermediate order, and type (by [`type_id`](#type_id_item) index) is the minor order. The list must not contain any duplicate entries.


- **method_ids** ([`method_id_item[]`](#method_id_item))  
  Method identifiers list. These are identifiers for all methods referred to by this file, whether defined in the file or not. This list must be sorted, where the defining type (by [`type_id`](#type_id_item) index) is the major order, method name (by [`string_id`](#string_id_item) index) is the intermediate order, and method prototype (by [`proto_id`](#proto_id_item) index) is the minor order. The list must not contain any duplicate entries.


- **class_defs** ([`class_def_item[]`](#class_def_item))  
  Class definitions list. The classes must be ordered such that a given class's superclass and implemented interfaces appear in the list earlier than the referring class. Furthermore, it is invalid for a definition for the same-named class to appear more than once in the list.


- **call_site_ids** ([`call_site_id_item[]`](#call_site_id_item))  
  Call site identifiers list. These are identifiers for all call sites referred to by this file, whether defined in the file or not. This list must be sorted in ascending order of `call_site_id`.


- **method_handles** ([`method_handle_item[]`](#method_handle_item))  
  Method handles list. A list of all method handles referred to by this file, whether defined in the file or not. This list is not sorted and may contain duplicates which will logically correspond to different method handle instances.


- **data** (`ubyte[]`)  
  Data area, containing all the support data for the lists above. Different items have different alignment requirements, and padding bytes are inserted before each item if necessary to achieve proper alignment.


- **link_data** (`ubyte[]`)
  Data used in statically linked files. The format of the data in this section is left unspecified by this document. This section is empty in unlinked files, and runtime implementations may use it as they see fit.


<a id="header_item"></a>
## `header_item`

The **header_item** appears at the very beginning of a `.dex` file. It contains the "map" of the rest of the file, providing offsets and sizes for all other major sections.

**Alignment:** 4 bytes

---
* **magic** (`ubyte[8]`)
  - The magic value identifying the file as a DEX file. Usually `dex\n035\0` or similar.


* **checksum** (`uint`)
  - An Adler-32 checksum of the rest of the file (excluding `magic` and this field).


* **signature** (`ubyte[20]`)
  - A SHA-1 signature (hash) of the rest of the file (excluding `magic`, `checksum`, and this field).


* **file_size** (`uint`)
  - The total size of the entire file in bytes. (v41+: distance to next header or end of container).
 
 
* **header_size** (`uint`)
  - The size of this header section in bytes.
  - `0x70` (112 bytes) for v40 or earlier.
  - `0x78` (120 bytes) for v41 or later.
 
 
* **endian_tag** (`uint`)
  - Set to `ENDIAN_CONSTANT` (0x12345678) to indicate byte order.
 
 
* **link_size** (`uint`)
  - Size of the link section, or 0 if not statically linked.
 
 
* **link_off** (`uint`)
  - Offset from the start of the file to the link section.


* **map_off** (`uint`)
  - Offset from the start of the file to the [`map_list`](#map_list).
 

* **string_ids_size / string_ids_off** (`uint`)
  - Count and file offset of the [`string_id_item[]`](#string_id_item) list.
 
 
* **type_ids_size / type_ids_off** (`uint`)
  - Count and file offset of the [`type_id_item[]`](#type_id_item) list (max 65535).
 
 
* **proto_ids_size / proto_ids_off** (`uint`)
  - Count and file offset of the [`proto_id_item[]`](#proto_id_item) list (max 65535).
 
 
* **field_ids_size / field_ids_off** (`uint`)
  - Count and file offset of the [`field_id_item[]`](#field_id_item) list.
 
 
* **method_ids_size / method_ids_off** (`uint`)
  - Count and file offset of the [`method_id_item[]`](#method_id_item) list.
 
 
* **class_defs_size / class_defs_off** (`uint`)
  - Count and file offset of the [`class_def_item[]`](#class_def_item) list.
 
 
* **data_size** (`uint`)
  - **v40-:** Size of the data section in bytes.
  - **v41+:** Unused.
 

* **data_off** (`uint`)
  - **v40-:** Offset to the start of the data section.
  - **v41+:** Unused.
 
 
* **container_size** (`uint`)
  - **v40-:** Assumed equal to `file_size`.
  - **v41+:** Size of the entire container (including other dex headers).


* **header_offset** (`uint`)
  - **v40-:** Assumed equal to 0.
  - **v41+:** Offset from the start of the file to the start of this header.

---

<a id="map_list"></a>
## map_list
Appears in the **data** section and is referenced from the [`header_item`](#header_item).
**Alignment:** 4 bytes

This is a list of the entire contents of a file, in order. It contains some redundancy with respect to the `header_item` but is intended to be an easy form to use to iterate over an entire file. A given type must appear at most once in a map, but there is no restriction on what order types may appear in, other than the restrictions implied by the rest of the format. Additionally, the map entries must be ordered by initial offset and must not overlap.

* **size** (`uint`)
  - Size of the list, in entries.
* **list** (`map_item[size]`)
  - Elements of the list.

---

<a id="map_item"></a>
## map_item format

* **type** (`ushort`)
  - Type of the items; see the Type Codes below.
 
 
* **unused** (`ushort`)
  - (unused)
 
 
* **size** (`uint`)
  - Count of the number of items to be found at the indicated offset.
 

* **offset** (`uint`)
  - Offset from the start of the file to the items in question.

---

## Type codes

* **TYPE_HEADER_ITEM** (`0x0000`)
  - Item Size: `0x70` - See [`header_item`](#header_item)
 
 
* **TYPE_STRING_ID_ITEM** (`0x0001`)
  - Item Size: `0x04` - See [`string_id_item`](#string_id_item)
 
 
* **TYPE_TYPE_ID_ITEM** (`0x0002`)
  - Item Size: `0x04` - See [`type_id_item`](#type_id_item)
 
 
* **TYPE_PROTO_ID_ITEM** (`0x0003`)
  - Item Size: `0x0c` - See [`proto_id_item`](#proto_id_item)
 
 
* **TYPE_FIELD_ID_ITEM** (`0x0004`)
  - Item Size: `0x08` - See [`field_id_item`](#field_id_item)
 
 
* **TYPE_METHOD_ID_ITEM** (`0x0005`)
  - Item Size: `0x08` - See [`method_id_item`](#method_id_item)
 
 
* **TYPE_CLASS_DEF_ITEM** (`0x0006`)
  - Item Size: `0x20` - See [`class_def_item`](#class_def_item)
 
 
* **TYPE_CALL_SITE_ID_ITEM** (`0x0007`)
  - Item Size: `0x04` - See [`call_site_id_item`](#call_site_id_item)
 
 
* **TYPE_METHOD_HANDLE_ITEM** (`0x0008`)
  - Item Size: `0x08` - See [`method_handle_item`](#method_handle_item)
 
 
* **TYPE_MAP_LIST** (`0x1000`)
  - Item Size: `4 + (item.size * 12)`
 
 
* **TYPE_TYPE_LIST** (`0x1001`)
  - Item Size: `4 + (item.size * 2)`. See [`type_list`](#type_list).
 
 
* **TYPE_ANNOTATION_SET_REF_LIST** (`0x1002`)
  - Item Size: `4 + (item.size * 4)`
 
 
* **TYPE_ANNOTATION_SET_ITEM** (`0x1003`)
  - Item Size: `4 + (item.size * 4)`
 
 
* **TYPE_CLASS_DATA_ITEM** (`0x2000`)
  - Item Size: Implicit; must parse. See [`class_data_item`](#class_data_item).
 
 
* **TYPE_CODE_ITEM** (`0x2001`)
  - Item Size: Implicit; must parse. See [`code_item`](#code_item).
 
 
* **TYPE_STRING_DATA_ITEM** (`0x2002`)
  - Item Size: Implicit; must parse.
 
 
* **TYPE_DEBUG_INFO_ITEM** (`0x2003`)
  - Item Size: Implicit; must parse.
 
 
* **TYPE_ANNOTATION_ITEM** (`0x2004`)
  - Item Size: Implicit; must parse.
 
 
* **TYPE_ENCODED_ARRAY_ITEM** (`0x2005`)
  - Item Size: Implicit; must parse. See [`encoded_array_item`](#encoded_array_item).
 
 
* **TYPE_ANNOTATIONS_DIRECTORY_ITEM** (`0x2006`)
  - Item Size: Implicit; must parse. See [`annotations_directory_item`](#annotations_directory_item).
 

* **TYPE_HIDDENAPI_CLASS_DATA_ITEM** (`0xF000`)
  - Item Size: Implicit; must parse.


<a id="string_id_item"></a>
## string_id_item
Appears in the **string_ids** section.
**Alignment:** 4 bytes

* **string_data_off** (`uint`)
  * Offset from the start of the file to the string data for this item.
  * The offset should be to a location in the **data** section.
  * The data should be in the format specified by [`string_data_item`](#string_data_item).
  * There is no alignment requirement for the offset.

---

<a id="string_data_item"></a>
## string_data_item
Appears in the **data** section.
**Alignment:** none (byte-aligned)

* **utf16_size** (`uleb128`)
  * Size of this string in UTF-16 code units (the "string length" in many systems).
  * This is the decoded length of the string.
  * The encoded length is implied by the position of the `0` byte.
 

* **data** (`ubyte[]`)
  * A series of MUTF-8 code units (a.k.a. octets, a.k.a. bytes).
  * Followed by a byte of value `0`.

> **Note:** It is acceptable to have a string which includes (the encoded form of) UTF-16 surrogate code units (that is, **U+d800 ... U+dfff**) either in isolation or out-of-order with respect to the usual encoding of Unicode into UTF-16. It is up to higher-level uses of strings to reject such invalid encodings, if appropriate.

---

<a id="type_id_item"></a>
## type_id_item
Appears in the **type_ids** section.
**Alignment:** 4 bytes

* **descriptor_idx** (`uint`)
  - Index into the [`string_ids`](#string_id_item) list for the descriptor string of this type.
  - The string must conform to the syntax for `TypeDescriptor`.

---

<a id="proto_id_item"></a>
## proto_id_item
Appears in the **proto_ids** section.
**Alignment:** 4 bytes

* **shorty_idx** (`uint`)
  - Index into the [`string_ids`](#string_id_item) list for the short-form descriptor string of this prototype.
  - The string must conform to the syntax for `ShortyDescriptor`, and must correspond to the return type and parameters of this item.
 
 
* **return_type_idx** (`uint`)
  - Index into the [`type_ids`](#type_id_item) list for the return type of this prototype.
 

* **parameters_off** (`uint`)
  - Offset from the start of the file to the list of parameter types for this prototype, or `0` if this prototype has no parameters.
  - This offset, if non-zero, should be in the **data** section, and the data there should be in the format specified by [`type_list`](#type_list).
  - Additionally, there should be no reference to the type `void` in the list.

---

<a id="field_id_item"></a>
## field_id_item
Appears in the **field_ids** section.  
**Alignment:** 4 bytes

* **class_idx** (`ushort`)
  - Index into the [`type_ids`](#type_id_item) list for the definer of this field.
  - This must be a class type, and not an array or primitive type.
 
 
* **type_idx** (`ushort`)
  - Index into the [`type_ids`](#type_id_item) list for the type of this field.
 

* **name_idx** (`uint`)
  - Index into the [`string_ids`](#string_id_item) list for the name of this field.
  - The string must conform to the syntax for `MemberName`.

---

<a id="method_id_item"></a>
## method_id_item
Appears in the **method_ids** section.  
**Alignment:** 4 bytes

* **class_idx** (`ushort`)
  - Index into the [`type_ids`](#type_id_item) list for the definer of this method.
  - This must be a class or array type, and not a primitive type.
 
 
* **proto_idx** (`ushort`)
  - Index into the [`proto_ids`](#proto_id_item) list for the prototype of this method.
 

* **name_idx** (`uint`)
  - Index into the [`string_ids`](#string_id_item) list for the name of this method.
  - The string must conform to the syntax for `MemberName`.

---

<a id="class_def_item"></a>
## class_def_item
Appears in the **class_defs** section.
**Alignment:** 4 bytes

* **class_idx** (`uint`)
  - Index into the [`type_ids`](#type_id_item) list for this class.
  - This must be a class type, and not an array or primitive type.
 
 
* **access_flags** (`uint`)
  - Access flags for the class (e.g., `public`, `final`).
  - See "access_flags Definitions" for details.
 
 
* **superclass_idx** (`uint`)
  - Index into the [`type_ids`](#type_id_item) list for the superclass, or the constant value `NO_INDEX` if this class has no superclass (i.e., it is a root class such as `Object`).
  - If present, this must be a class type, and not an array or primitive type.
 
 
* **interfaces_off** (`uint`)
  - Offset from the start of the file to the list of interfaces, or `0` if there are none.
  - This offset should be in the **data** section, and the data there should be in the format specified by [`type_list`](#type_list).
  - Each element of the list must be a class type (not an array or primitive type), and there must not be any duplicates.
 
 
* **source_file_idx** (`uint`)
  - Index into the [`string_ids`](#string_id_item) list for the name of the file containing the original source for (at least most of) this class, or the special value `NO_INDEX` to represent a lack of this information.
  - The `debug_info_item` of any given method may override this source file, but the expectation is that most classes will only come from one source file.
 
 
* **annotations_off** (`uint`)
  - Offset from the start of the file to the annotations structure for this class, or `0` if there are no annotations on this class.
  - This offset, if non-zero, should be in the **data** section, and the data there should be in the format specified by [`annotations_directory_item`](#annotations_directory_item), with all items referring to this class as the definer.
 
 
* **class_data_off** (`uint`)
  - Offset from the start of the file to the associated class data for this item, or `0` if there is no class data for this class.
  - (This may be the case, for example, if this class is a marker interface.)
  - The offset, if non-zero, should be in the **data** section, and the data there should be in the format specified by [`class_data_item`](#class_data_item), with all items referring to this class as the definer.
 

* **static_values_off** (`uint`)
  - Offset from the start of the file to the list of initial values for `static` fields, or `0` if there are none (and all `static` fields are to be initialized with `0` or `null`).
  - This offset should be in the **data** section, and the data there should be in the format specified by [`encoded_array_item`](#encoded_array_item).
  - The size of the array must be no larger than the number of `static` fields declared by this class, and the elements correspond to the `static` fields in the same order as declared in the corresponding `field_list`.
  - The type of each array element must match the declared type of its corresponding field.
  - If there are fewer elements in the array than there are `static` fields, then the leftover fields are initialized with a type-appropriate `0` or `null`.

---

<a id="call_site_id_item"></a>
## call_site_id_item
Appears in the **call_site_ids** section.
**Alignment:** 4 bytes

* **call_site_off** (`uint`)
  - Offset from the start of the file to the call site definition.
  - The offset should be in the **data** section, and the data there should be in the format specified by [`call_site_item`](#call_site_item).

---

<a id="method_handle_item"></a>
## method_handle_item
Appears in the **method_handles** section.
**Alignment:** 4 bytes

* **method_handle_type** (`ushort`)
  - Type of the method handle; see the type codes below.
 
 
* **unused** (`ushort`)
  - (unused)
 

* **field_or_method_id** (`ushort`)
  - Field or method ID depending on whether the method handle type is an accessor or a method invoker.
 

* **unused** (`ushort`)
  - (unused)

---

## Method handle type codes

* **METHOD_HANDLE_TYPE_STATIC_PUT** (`0x00`)
  - Method handle is a static field setter (accessor).
 
 
* **METHOD_HANDLE_TYPE_STATIC_GET** (`0x01`)
  - Method handle is a static field getter (accessor).

 
* **METHOD_HANDLE_TYPE_INSTANCE_PUT** (`0x02`)
  - Method handle is an instance field setter (accessor).
 
 
* **METHOD_HANDLE_TYPE_INSTANCE_GET** (`0x03`)
  - Method handle is an instance field getter (accessor).
 
 
* **METHOD_HANDLE_TYPE_INVOKE_STATIC** (`0x04`)
  - Method handle is a static method invoker.
 
 
* **METHOD_HANDLE_TYPE_INVOKE_INSTANCE** (`0x05`)
  - Method handle is an instance method invoker.
 
 
* **METHOD_HANDLE_TYPE_INVOKE_CONSTRUCTOR** (`0x06`)
  - Method handle is a constructor method invoker.
 
 
* **METHOD_HANDLE_TYPE_INVOKE_DIRECT** (`0x07`)
  - Method handle is a direct method invoker.
 

* **METHOD_HANDLE_TYPE_INVOKE_INTERFACE** (`0x08`)
  - Method handle is an interface method invoker.


<a id="type_list"></a>
## type_list
Appears in the **data** section.
**Alignment:** 4 bytes

* **size** (`uint`)
  * Size of the list, in entries.
* **list** (`type_item[size]`)
  * Elements of the list.

---

<a id="type_item"></a>
## type_item format

* **type_idx** (`ushort`)
  * Index into the [`type_ids`](#type_id_item) list.


<a id="code_item"></a>
## code_item
Appears in the **data** section.
**Alignment:** 4 bytes

* **registers_size** (`ushort`)
  - The number of registers used by this code.
* **ins_size** (`ushort`)
  - The number of words of incoming arguments to the method that this code is for.
* **outs_size** (`ushort`)
  - The number of words of outgoing argument space required by this code for method invocation.
* **tries_size** (`ushort`)
  - The number of [`try_item`](#try_item)s for this instance. If non-zero, then these appear as the `tries` array just after the `insns` in this instance.
* **debug_info_off** (`uint`)
  - Offset from the start of the file to the debug info (line numbers + local variable info) sequence for this code, or `0` if there simply is no information.
  - The offset, if non-zero, should be to a location in the **data** section.
  - The format of the data is specified by `debug_info_item` below.
* **insns_size** (`uint`)
  - Size of the instructions list, in 16-bit code units.
* **insns** (`ushort[insns_size]`)
  - Actual array of bytecode. The format of code in an `insns` array is specified by the companion document Dalvik bytecode.
  - Note that though this is defined as an array of `ushort`, there are some internal structures that prefer four-byte alignment.
  - Also, if this happens to be in an endian-swapped file, then the swapping is *only* done on individual `ushort` instances and not on the larger internal structures.
* **padding** (`ushort`, optional)
  - Two bytes of padding to make `tries` four-byte aligned. This element is only present if `tries_size` is non-zero and `insns_size` is odd.
* **tries** (`try_item[tries_size]`, optional)
  - Array indicating where in the code exceptions are caught and how to handle them.
  - Elements of the array must be non-overlapping in range and in order from low to high address.
  - This element is only present if `tries_size` is non-zero.
* **handlers** ([`encoded_catch_handler_list`](#encoded_catch_handler_list), optional)
  - Bytes representing a list of lists of catch types and associated handler addresses.
  - Each [`try_item`](#try_item) has a byte-wise offset into this structure.
  - This element is only present if `tries_size` is non-zero.

---

<a id="try_item"></a>
## try_item format

* **start_addr** (`uint`)
  - Start address of the block of code covered by this entry. The address is a count of 16-bit code units to the start of the first covered instruction.
* **insn_count** (`ushort`)
  - Number of 16-bit code units covered by this entry. The last code unit covered (inclusive) is `start_addr + insn_count - 1`.
* **handler_off** (`ushort`)
  - Offset in bytes from the start of the associated [`encoded_catch_handler_list`](#encoded_catch_handler_list) to the [`encoded_catch_handler`](#encoded_catch_handler) for this entry.
  - This must be an offset to the start of an [`encoded_catch_handler`](#encoded_catch_handler).

---

<a id="encoded_catch_handler_list"></a>
## encoded_catch_handler_list format

* **size** (`uleb128`)
  - Size of this list, in entries.
* **list** (`encoded_catch_handler[handlers_size]`)
  - Actual list of handler lists, represented directly (not as offsets), and concatenated sequentially.

---

<a id="encoded_catch_handler"></a>
## encoded_catch_handler format

* **size** (`sleb128`)
  - Number of catch types in this list. If non-positive, then this is the negative of the number of catch types, and the catches are followed by a catch-all handler.
  - For example: A `size` of `0` means that there is a catch-all but no explicitly typed catches. A `size` of `2` means that there are two explicitly typed catches and no catch-all. And a `size` of `-1` means that there is one typed catch along with a catch-all.
* **handlers** (`encoded_type_addr_pair[abs(size)]`)
  - Stream of `abs(size)` encoded items, one for each caught type, in the order that the types should be tested.
* **catch_all_addr** (`uleb128`, optional)
  - Bytecode address of the catch-all handler. This element is only present if `size` is non-positive.

---

<a id="encoded_type_addr_pair"></a>
## encoded_type_addr_pair format

* **type_idx** (`uleb128`)
  - Index into the [`type_ids`](#type_id_item) list for the type of the exception to catch.
* **addr** (`uleb128`)
  - Bytecode address of the associated exception handler.


<a id="class_data_item"></a>
## class_data_item
Appears in the **data** section.
**Alignment:** none (byte-aligned)

* **static_fields_size** (`uleb128`)
  - The number of static fields defined in this item.
* **instance_fields_size** (`uleb128`)
  - The number of instance fields defined in this item.
* **direct_methods_size** (`uleb128`)
  - The number of direct methods defined in this item.
* **virtual_methods_size** (`uleb128`)
  - The number of virtual methods defined in this item.
* **static_fields** ([`encoded_field`](#encoded_field)[static_fields_size])
  - The defined static fields, represented as a sequence of encoded elements. The fields must be sorted by `field_idx` in increasing order.
* **instance_fields** ([`encoded_field`](#encoded_field)[instance_fields_size])
  - The defined instance fields, represented as a sequence of encoded elements. The fields must be sorted by `field_idx` in increasing order.
* **direct_methods** ([`encoded_method`](#encoded_method)[direct_methods_size])
  - The defined direct (any of `static`, `private`, or `constructor`) methods, represented as a sequence of encoded elements. The methods must be sorted by `method_idx` in increasing order.
* **virtual_methods** ([`encoded_method`](#encoded_method)[virtual_methods_size])
  - The defined virtual (none of `static`, `private`, or `constructor`) methods, represented as a sequence of encoded elements. This list should *not* include inherited methods unless overridden by the class that this item represents. The methods must be sorted by `method_idx` in increasing order. The `method_idx` of a virtual method must *not* be the same as any direct method.

> **Note**: All elements' field_id and method_id instances must refer to the same defining class.

<a id="encoded_method"></a>
## encoded_method format

* **method_idx_diff** (`uleb128`)
  - Index into the [`method_ids`](#method_id_item) list for the identity of this method (includes the name and descriptor), represented as a difference from the index of previous element in the list. The index of the first element in a list is represented directly.
* **access_flags** (`uleb128`)
  - Access flags for the method (`public`, `final`, etc.). See "access_flags Definitions" for details.
* **code_off** (`uleb128`)
  - Offset from the start of the file to the code structure for this method, or `0` if this method is either `abstract` or `native`. The offset should be to a location in the **data** section. The format of the data is specified by [`code_item`](#code_item).

---

<a id="encoded_field"></a>
## encoded_field format

* **field_idx_diff** (`uleb128`)
  - Index into the [`field_ids`](#field_id_item) list for the identity of this field (includes the name and descriptor), represented as a difference from the index of previous element in the list. The index of the first element in a list is represented directly.
* **access_flags** (`uleb128`)
  - Access flags for the field (`public`, `final`, etc.). See "access_flags Definitions" for details.


<a id="call_site_item"></a>
## call_site_item
Appears in the **data** section.
**Alignment:** none (byte-aligned)

The `call_site_item` is an [`encoded_array_item`](#encoded_array_item) whose elements correspond to the arguments provided to a bootstrap linker method.

### Initial Arguments
The first three arguments are:
1. A method handle representing the bootstrap linker method (`VALUE_METHOD_HANDLE`).
2. A method name that the bootstrap linker should resolve (`VALUE_STRING`).
3. A method type corresponding to the type of the method name to be resolved (`VALUE_METHOD_TYPE`).

Any additional arguments are constant values passed to the bootstrap linker method in order and without type conversions.

### Linker Method Requirements
The method handle representing the bootstrap linker method must have the return type `java.lang.invoke.CallSite`. The first three parameter types are:
1. `java.lang.invoke.Lookup`
2. `java.lang.invoke.String`
3. `java.lang.invoke.MethodType`

The parameter types of any additional arguments are determined from their constant values.

<a id="encoded_array_item"></a>
## encoded_array_item
Appears in the **data** section.
**Alignment:** none (byte-aligned)

* **value** (`encoded_array`)
  - Bytes representing the encoded array value, in the format specified by "encoded_array Format" under "encoded_value Encoding".


<a id="annotations_directory_item"></a>
## annotations_directory_item
Appears in the **data** section.
**Alignment:** 4 bytes

* **class_annotations_off** (`uint`)
  - Offset from the start of the file to the annotations made directly on the class, or `0` if the class has no direct annotations. The offset, if non-zero, should be to a location in the **data** section. The format of the data is specified by `annotation_set_item` below.
* **fields_size** (`uint`)
  - Count of fields annotated by this item.
* **annotated_methods_size** (`uint`)
  - Count of methods annotated by this item.
* **annotated_parameters_size** (`uint`)
  - Count of method parameter lists annotated by this item.
* **field_annotations** (`field_annotation[fields_size]`, optional)
  - List of associated field annotations. The elements of the list must be sorted in increasing order, by `field_idx`.
* **method_annotations** (`method_annotation[methods_size]`, optional)
  - List of associated method annotations. The elements of the list must be sorted in increasing order, by `method_idx`.
* **parameter_annotations** (`parameter_annotation[parameters_size]`, optional)
  - List of associated method parameter annotations. The elements of the list must be sorted in increasing order, by `method_idx`.

> **Note:** All elements' `field_id` and `method_id` instances must refer to the same defining class.

---

<a id="field_annotation"></a>
## field_annotation format

* **field_idx** (`uint`)
  - Index into the [`field_ids`](#field_id_item) list for the identity of the field being annotated.
* **annotations_off** (`uint`)
  - Offset from the start of the file to the list of annotations for the field. The offset should be to a location in the **data** section. The format of the data is specified by `annotation_set_item` below.

---

<a id="method_annotation"></a>
## method_annotation format

* **method_idx** (`uint`)
  - Index into the [`method_ids`](#method_id_item) list for the identity of the method being annotated.
* **annotations_off** (`uint`)
  - Offset from the start of the file to the list of annotations for the method. The offset should be to a location in the **data** section. The format of the data is specified by `annotation_set_item` below.

---

<a id="parameter_annotation"></a>
## parameter_annotation format

* **method_idx** (`uint`)
  - Index into the [`method_ids`](#method_id_item) list for the identity of the method whose parameters are being annotated.
* **annotations_off** (`uint`)
  - Offset from the start of the file to the list of annotations for the method parameters. The offset should be to a location in the **data** section. The format of the data is specified by `annotation_set_ref_list` below.
