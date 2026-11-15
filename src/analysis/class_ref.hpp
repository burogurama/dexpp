#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "class_fwd.hpp"

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** A reference to a class by its type descriptor, which may or may not be
 *  defined in the loaded DEX files: the superclass, the interfaces and the
 *  declaring class of a member are ClassRefs, because they can name a class
 *  from outside the app, such as "Ljava/lang/Object;".
 *
 *  Get one from Class::superclass(), Class::interfaces(),
 *  Method::declaring_class() or Field::declaring_class().  ClassRef keeps
 *  shared ownership of the analysis, so it stays valid after every
 *  AnalysisContext wrapper has been destroyed, and it owns its descriptor.
 *  To look a class up by a descriptor you have, use
 *  AnalysisContext::find_class(). */
class ClassRef
{
  public:
    /** The 'L...;' type descriptor string (e.g. "Ljava/lang/Object;").  The
     *  returned view is valid for this ClassRef's lifetime. */
    std::string_view descriptor() const { return descriptor_; }

    /** Looks up the class in the analysis it came from, the same way
     *  AnalysisContext::find_class() does.
     *  @return the Class, or std::nullopt if no loaded DEX file defines it. */
    std::optional<Class> resolve() const;

    /** True when resolve() would find the class. */
    bool is_resolved() const;

    /** Two references are equal when they come from the same analysis (one
     *  AnalysisContext, or copies of it) and name the same descriptor.
     *  References from two contexts that loaded the same file are not equal.
     *  std::hash<ClassRef> is consistent with it. */
    bool operator==(const ClassRef &) const = default;

  private:
    friend struct std::hash<ClassRef>;
    // The library makes the references; these are the places that do.
    friend class Class;
    friend class Method;
    friend class Field;

    ClassRef(std::string_view descriptor, std::shared_ptr<const detail::AnalysisContextImpl> impl);

    std::string descriptor_;
    std::shared_ptr<const detail::AnalysisContextImpl> impl_;
};

} // namespace dex

namespace std {

/** Hash of a ClassRef, consistent with ClassRef::operator==, so references can
 *  be keys of std::unordered_map and std::unordered_set. */
template <> struct hash<dex::ClassRef>
{
    std::size_t operator()(const dex::ClassRef &ref) const noexcept;
};

} // namespace std
