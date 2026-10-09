#Parser API Design for dex++

## Overview

Redesign the Parser class to support advanced DEX file analysis including call graph construction, basic block analysis, and control flow analysis for APK analysis workflows.

## Current Issues

1. **Stateful Design**: Parser maintains internal state (`off`, `strings`, etc.) which is error-prone and not thread-safe
2. **Mixed Concerns**: Parsing logic mixed with state management
3. **Inconsistent API**: Some methods return values, some modify internal state
4. **Offset Management**: Manual offset tracking is brittle and error-prone
5. **No Error Handling**: No validation or error reporting mechanism
6. **Limited Extensibility**: Hard to add new parsing features for analysis

## Design Philosophy

### 1. Immutable/Functional Approach
- Parser methods should be pure functions where possible
- Input: buffer + offset → Output: parsed value + new offset
- No internal state mutations during parsing
- Thread-safe by design

### 2. Clear Separation of Concerns
- **Parsing Layer**: Extract raw data structures from DEX file
- **Model Layer**: High-level DEX representation with convenience methods
- **Analysis Layer**: Build abstractions like call graphs, CFGs, dominator trees

### 3. Composable API
- Methods can be chained or composed
- Each method parses a specific DEX structure
- Progressive parsing: parse only what you need
- Support for both eager and lazy parsing

### 4. Explicit Error Handling
- Use `std::expected<T, ParseError>` for all parsing operations
- Validate DEX file structure and constraints
- Report malformed files with location info (offset, context)

### 5. Performance Considerations
- Minimize memory allocations during parsing
- Support lazy/partial parsing for large files
- Cache only when beneficial (must be explicit)
- Use span and string_view for zero-copy where possible

## Proposed API Design

```cpp
#ifndef PARSER_HPP
#define PARSER_HPP

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "class_file/types.hpp"
#include "utils/parsing_utils.hpp"

namespace dex
{

    // Forward declarations
    class DEXFile;
    class AnalysisContext;

    /**
     * Parse error with context information
     */
    struct ParseError
    {
        enum class Code {
            InvalidMagic,       // DEX magic mismatch
            TruncatedFile,      // File is too short
            InvalidOffset,      // Offset points outside file
            InvalidSize,        // Invalid item count/size
            MalformedData,      // Data doesn't conform to DEX spec
            OutOfBounds,        // Read beyond buffer
            UnsupportedVersion, // DEX version not supported
            InvalidUtf8String,  // String data is malformed
            InvalidLeb128,      // LEB128 encoding is invalid
            InvalidOpcode,      // Unknown or invalid instruction
            NullPointer,        // Required pointer is null
            UnknownError        // Unexpected error
        };

        Code code;
        std::string message; // Human-readable error description
        uint32_t offset;     // Where the error occurred (if applicable)

        // Factory methods for common errors
        static ParseError invalid_magic(uint32_t offset);
        static ParseError truncated_file(uint32_t expected, uint32_t actual);
        static ParseError invalid_offset(uint32_t offset, uint32_t file_size);
    };

    /**
     * Analysis error (separate from parse errors)
     */
    struct AnalysisError
    {
        enum class Code {
            MissingSection,     // Required section not found
            CircularDependency, // Circular relationship detected
            InvalidBytecode,    // Bytecode is malformed
            StackOverflow,      // Analysis recursion too deep
            Timeout,            // Analysis took too long
            OutOfMemory,        // Analysis ran out of memory
            UnknownError        // Unexpected error
        };

        Code code;
        std::string message;
        std::optional<uint32_t> location; // Optional location info
    };

    /**
     * Main Parser class - stateless, functional parsing API
     */
    class Parser
    {
      public:
        // Disable construction (all methods are static)
        Parser() = delete;
        Parser(const Parser &) = delete;
        Parser &operator=(const Parser &) = delete;

        // ============ Top-level parsing ============

        /**
         * Parse entire DEX file from file path (eager parsing)
         */
        static std::expected<DEXFile, ParseError> parse_file(const std::string &path);

        /**
         * Parse DEX file from buffer (eager parsing)
         */
        static std::expected<DEXFile, ParseError> parse_buffer(std::span<const uint8_t> buffer);

        /**
         * Parse DEX file lazily (parse sections on-demand)
         */
        static std::expected<DEXFile, ParseError> parse_file_lazy(const std::string &path);

        /**
         * Verify DEX file integrity (checksum, signature, structure)
         */
        static std::expected<void, ParseError> verify(std::span<const uint8_t> buffer);

        // ============ Low-level parsing (stateless) ============

        /**
         * Parse DEX header from buffer
         * @param buf Buffer containing DEX data
         * @param offset Offset to start parsing from
         * @return Header or error
         */
        static std::expected<Header, ParseError> parse_header(std::span<const uint8_t> buf,
                                                              uint32_t offset = 0);

        /**
         * Parse map list (DEX file index)
         * @param buf Buffer containing DEX data
         * @param offset Offset to map list
         * @return MapList or error
         */
        static std::expected<MapList, ParseError> parse_map_list(std::span<const uint8_t> buf,
                                                                 uint32_t offset);

        /**
         * Parse string ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to string IDs
         * @param count Number of string IDs to parse
         * @return Vector of string IDs and new offset
         */
        static std::expected<std::pair<std::vector<uint32_t>, uint32_t>, ParseError>
        parse_string_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse type ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to type IDs
         * @param count Number of type IDs to parse
         * @return Vector of type IDs and new offset
         */
        static std::expected<std::pair<std::vector<uint32_t>, uint32_t>, ParseError>
        parse_type_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse prototype ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to prototype IDs
         * @param count Number of prototype IDs to parse
         * @return Vector of ProtoIdItem and new offset
         */
        static std::expected<std::pair<std::vector<ProtoIdItem>, uint32_t>, ParseError>
        parse_proto_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse field ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to field IDs
         * @param count Number of field IDs to parse
         * @return Vector of FieldIdItem and new offset
         */
        static std::expected<std::pair<std::vector<FieldIdItem>, uint32_t>, ParseError>
        parse_field_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse method ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to method IDs
         * @param count Number of method IDs to parse
         * @return Vector of MethodIdItem and new offset
         */
        static std::expected<std::pair<std::vector<MethodIdItem>, uint32_t>, ParseError>
        parse_method_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse class definition list
         * @param buf Buffer containing DEX data
         * @param offset Offset to class definitions
         * @param count Number of class definitions to parse
         * @return Vector of ClassDefItem and new offset
         */
        static std::expected<std::pair<std::vector<ClassDefItem>, uint32_t>, ParseError>
        parse_class_defs(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse call site ID list
         * @param buf Buffer containing DEX data
         * @param offset Offset to call site IDs
         * @param count Number of call site IDs to parse
         * @return Vector of CallSiteIdItem and new offset
         */
        static std::expected<std::pair<std::vector<CallSiteIdItem>, uint32_t>, ParseError>
        parse_call_site_ids(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        /**
         * Parse method handle list
         * @param buf Buffer containing DEX data
         * @param offset Offset to method handles
         * @param count Number of method handles to parse
         * @return Vector of MethodHandleItem and new offset
         */
        static std::expected<std::pair<std::vector<MethodHandleItem>, uint32_t>, ParseError>
        parse_method_handles(std::span<const uint8_t> buf, uint32_t offset, uint32_t count);

        // ============ String and code parsing ============

        /**
         * Parse single string from string data section
         * @param buf Buffer containing DEX data
         * @param offset Offset to string data
         * @return String value and new offset
         */
        static std::expected<std::pair<std::string, uint32_t>, ParseError>
        parse_string(std::span<const uint8_t> buf, uint32_t offset);

        /**
         * Parse all strings from string data section
         * @param buf Buffer containing DEX data
         * @param string_ids List of string IDs (offsets)
         * @return Vector of strings
         */
        static std::expected<std::vector<std::string>, ParseError>
        parse_strings(std::span<const uint8_t> buf, const std::vector<uint32_t> &string_ids);

        /**
         * Parse single code item (method implementation)
         * @param buf Buffer containing DEX data
         * @param offset Offset to code item
         * @return CodeItem or error
         */
        static std::expected<CodeItem, ParseError> parse_code_item(std::span<const uint8_t> buf,
                                                                   uint32_t offset);

        /**
         * Parse single instruction (for manual CFG construction)
         * @param buf Buffer containing DEX data
         * @param offset Offset to instruction
         * @return Instruction and new offset
         */
        static std::expected<std::pair<Instruction, uint32_t>, ParseError>
        parse_instruction(std::span<const uint8_t> buf, uint32_t offset);

        /**
         * Parse instruction stream (all instructions in code item)
         * @param buf Buffer containing DEX data
         * @param offset Offset to first instruction
         * @param insns_size Number of instructions to parse
         * @return Vector of instructions and new offset
         */
        static std::expected<std::pair<std::vector<Instruction>, uint32_t>, ParseError>
        parse_instructions(std::span<const uint8_t> buf, uint32_t offset, uint32_t insns_size);

        // ============ Verification helpers ============

        /**
         * Verify and compute Adler-32 checksum of DEX file
         * @param buf Buffer containing DEX data
         * @return Computed checksum or error
         */
        static std::expected<uint32_t, ParseError> compute_checksum(std::span<const uint8_t> buf);

        /**
         * Verify and compute SHA-1 signature of DEX file
         * @param buf Buffer containing DEX data
         * @return Computed signature or error
         */
        static std::expected<std::array<uint8_t, 20>, ParseError>
        compute_signature(std::span<const uint8_t> buf);

        /**
         * Verify all internal offsets are valid and within bounds
         * @param buf Buffer containing DEX data
         * @return Success or error with details
         */
        static std::expected<void, ParseError> verify_offsets(std::span<const uint8_t> buf);
    };

    /**
     * High-level DEX file representation
     * This is what most analysis code will use
     */
    class DEXFile
    {
      private:
        Header header_;
        MapList map_list_;

        std::vector<std::string> string_table_;    // Resolved strings
        std::vector<TypeDescriptor> types_;        // Type descriptors
        std::vector<Prototype> prototypes_;        // Method prototypes
        std::vector<Field> fields_;                // Field definitions
        std::vector<Method> methods_;              // Method definitions
        std::vector<ClassDef> classes_;            // Class definitions
        std::vector<CallSite> call_sites_;         // Call sites
        std::vector<MethodHandle> method_handles_; // Method handles

        // Lazy parsing state (optional)
        mutable std::optional<std::vector<Method>> methods_cache_;
        bool fully_parsed_ = false;

        // Private constructor - use DEXFile::parse()
        DEXFile() = default;

      public:
        /**
         * Parse entire DEX file eagerly
         */
        static std::expected<DEXFile, ParseError> parse(std::span<const uint8_t> buffer);

        /**
         * Parse DEX file lazily (sections loaded on-demand)
         */
        static std::expected<DEXFile, ParseError> parse_lazy(std::span<const uint8_t> buffer);

        /**
         * Get DEX file header
         */
        const Header &get_header() const { return header_; }

        /**
         * Get map list (file index)
         */
        const MapList &get_map_list() const { return map_list_; }

        /**
         * Get string table (resolved strings)
         */
        const std::vector<std::string> &get_string_table() const { return string_table_; }

        /**
         * Get string by index
         * @param index String index
         * @return String view or error
         */
        std::expected<std::string_view, ParseError> get_string(size_t index) const;

        /**
         * Get type descriptor by index
         */
        std::expected<std::string_view, ParseError> get_type(size_t index) const;

        /**
         * Get method by index
         */
        const Method &get_method(size_t index) const { return methods_.at(index); }

        /**
         * Get all methods (forces lazy parsing if needed)
         */
        std::expected<std::reference_wrapper<const std::vector<Method>>, ParseError> get_methods();

        /**
         * Get class by index
         */
        const ClassDef &get_class(size_t index) const { return classes_.at(index); }

        /**
         * Find class by name
         */
        std::expected<const ClassDef *, ParseError> find_class(std::string_view name) const;

        /**
         * Find method by name and signature
         */
        std::expected<const Method *, ParseError> find_method(std::string_view class_name,
                                                              std::string_view method_name) const;

        /**
         * Get all classes defined in this DEX file
         */
        const std::vector<ClassDef> &get_classes() const { return classes_; }

        /**
         * Verify DEX file integrity
         */
        std::expected<void, ParseError> verify() const;

        /**
         * Save DEX file to path (serialize)
         */
        std::expected<void, ParseError> save(const std::string &path) const;
    };

} // namespace dex

#endif // PARSER_HPP
