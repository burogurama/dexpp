#pragma once

#include <optional>
#include <string_view>

#include "class_ref.hpp"

namespace dex {

class AnalysisContext;

/** An immutable view of a single DEX type descriptor string.
 *
 *  The string_view held by TypeDescriptor is only valid for the lifetime of the
 *  AnalysisContext that owns the underlying string storage.  TypeDescriptor itself
 *  is cheap to copy. */
class TypeDescriptor
{
  public:
    explicit TypeDescriptor(std::string_view descriptor);

    std::string_view descriptor() const { return descriptor_; }

    /** Returns true for class descriptors of the form 'L...;'. */
    bool is_class() const
    {
        return descriptor_.size() >= 2 && descriptor_.front() == 'L' && descriptor_.back() == ';';
    }

    /** Returns true for array descriptors that start with '['. */
    bool is_array() const { return !descriptor_.empty() && descriptor_.front() == '['; }

    /** Returns true for the eight primitive type codes: V B C D F I J S Z. */
    bool is_primitive() const;

    /** If this is a class descriptor, constructs and returns a ClassRef for it.
     *  Returns std::nullopt for primitive and array types. */
    std::optional<ClassRef> as_class_ref(const AnalysisContext *ctx) const;

    /** For array types, returns the descriptor of the element type.
     *  e.g. "[Ljava/lang/String;" → "Ljava/lang/String;", "[[I" → "[I".
     *  Returns std::nullopt for non-array types. */
    std::optional<TypeDescriptor> element_type() const;

  private:
    std::string_view descriptor_;
};

} // namespace dex
