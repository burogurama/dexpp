#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "access_flags.hpp"
#include "annotations.hpp"
#include "class_ref.hpp"
#include "type_descriptor.hpp"

namespace dex {

namespace detail {
struct AnalysisContextImpl;
} // namespace detail

/** A lightweight, non-owning handle to a single DEX field.
 *
 *  Instances are normally obtained from Class::fields().  The handle stores the
 *  absolute field_ids index and the access flags from the EncodedField entry in
 *  class_data_item. */
class Field
{
  public:
    /** @param field_abs_idx  Absolute index into the DEX file's field_ids table.
     *  @param access_flags   Flags from the enclosing EncodedField. */
    Field(std::shared_ptr<const detail::AnalysisContextImpl> impl, std::size_t dex_idx,
          uint32_t field_abs_idx, uint32_t access_flags);

    /** Simple name of the field (e.g. "out", "TAG"). */
    std::string_view name() const;

    /** ClassRef for the class that declares this field. */
    ClassRef declaring_class() const;

    /** The declared type of the field. */
    TypeDescriptor type() const;

    AccessFlags access_flags() const { return static_cast<AccessFlags>(access_flags_); }

    bool is_public() const { return has_flag(access_flags(), AccessFlags::Public); }
    bool is_static() const { return has_flag(access_flags(), AccessFlags::Static); }
    bool is_final() const { return has_flag(access_flags(), AccessFlags::Final); }

    /** Annotations attached to this field, resolved.  Parsed on demand. */
    std::vector<Annotation> annotations() const;

    /** The recorded initial value of a static field, from the class's
     *  static_values encoded array.  nullopt when the field is not static,
     *  has no recorded value, or sits past the array's prefix — all of which
     *  mean "the type's default value" (0 / null) at runtime. */
    std::optional<AnnotationValue> initial_value() const;

  private:
    std::shared_ptr<const detail::AnalysisContextImpl> impl_;
    std::size_t dex_idx_;
    uint32_t field_abs_idx_;
    uint32_t access_flags_;
};

} // namespace dex
