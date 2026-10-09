#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "access_flags.hpp"
#include "class_ref.hpp"
#include "field.hpp"
#include "method.hpp"

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** A lightweight, non-owning handle to a single DEX class definition.
 *
 *  Instances are normally obtained from AnalysisContext::classes() or
 *  AnalysisContext::find_class().  The handle keeps shared ownership of the
 *  underlying implementation so it remains valid even after every
 *  AnalysisContext wrapper has been destroyed.
 *
 *  A default-constructed Class has is_valid() == false and must not have any
 *  other method called on it. */
class Class
{
  public:
    Class(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
          uint32_t class_def_idx);

    /** The 'L...;' type descriptor that names this class (e.g. "LTestDex;"). */
    std::string_view name() const;

    /** The package name in slash-separated form (e.g. "java/lang" for
     *  "Ljava/lang/Object;").  Returns an empty string_view for classes in the
     *  default package. */
    std::string_view package() const;

    /** A ClassRef for the direct superclass, or std::nullopt for java.lang.Object
     *  itself (superclass_idx == NO_INDEX in the class_def_item). */
    std::optional<ClassRef> superclass() const;

    /** The interfaces implemented by this class, in declaration order. */
    std::vector<ClassRef> interfaces() const;

    /** All direct and virtual methods defined in this class, in that order. */
    std::vector<Method> methods() const;

    /** All static and instance fields declared in this class, in that order. */
    std::vector<Field> fields() const;

    /** Class-level annotations, resolved.  Parsed on demand. */
    std::vector<Annotation> annotations() const;

    AccessFlags access_flags() const;

    /** The source file name recorded in the class_def_item (e.g. "Foo.java"),
     *  or std::nullopt if the DEX file does not carry this information. */
    std::optional<std::string_view> source_file() const;

    bool is_public() const { return has_flag(access_flags(), AccessFlags::Public); }
    bool is_abstract() const { return has_flag(access_flags(), AccessFlags::Abstract); }
    bool is_interface() const { return has_flag(access_flags(), AccessFlags::Interface); }
    bool is_final() const { return has_flag(access_flags(), AccessFlags::Final); }
    bool is_enum() const { return has_flag(access_flags(), AccessFlags::Enum); }

    /** Returns false for a default-constructed "null" Class handle. */
    bool is_valid() const { return impl_ != nullptr; }

    /** Default constructor produces an invalid handle (is_valid() == false). */
    Class() : impl_(nullptr), dex_idx_(0), class_def_idx_(0) {}

  private:
    std::shared_ptr<const detail::AnalysisContextImpl> impl_;
    std::size_t dex_idx_;
    uint32_t class_def_idx_;
};

} // namespace dex
