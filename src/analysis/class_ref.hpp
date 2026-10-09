#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "class_fwd.hpp"

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

class AnalysisContext;

/** A lightweight, non-owning reference to a class identified by its type descriptor.
 *
 *  ClassRef keeps a shared_ptr to the underlying AnalysisContext implementation,
 *  so the referenced data stays alive for the ClassRef's lifetime regardless of
 *  whether the original AnalysisContext wrapper has been destroyed.  The
 *  descriptor is owned (copied in), so the public constructor is safe to call
 *  with a temporary string. */
class ClassRef
{
  public:
    /** Public constructor — accepts a context pointer.  The implementation pointer
     *  is captured via shared ownership, so the ClassRef survives even if the
     *  original AnalysisContext is later destroyed. */
    ClassRef(std::string_view descriptor, const AnalysisContext *ctx);

    /** Internal constructor used by other handles to forward shared ownership
     *  without an extra hop through a raw context pointer. */
    ClassRef(std::string_view descriptor, std::shared_ptr<const detail::AnalysisContextImpl> impl);

    /** The 'L...;' type descriptor string (e.g. "Ljava/lang/Object;").  The
     *  returned view is valid for this ClassRef's lifetime. */
    std::string_view descriptor() const { return descriptor_; }

    /** Looks up the class in the associated context.
     *  @return a Class handle for the resolved class, or a default-constructed
     *          (is_valid() == false) Class if the class is not defined in any
     *          loaded DEX file. */
    Class resolve() const;

    /** Returns true iff resolve() would return a valid handle. */
    bool is_resolved() const;

  private:
    std::string descriptor_;
    std::shared_ptr<const detail::AnalysisContextImpl> impl_;
};

} // namespace dex
