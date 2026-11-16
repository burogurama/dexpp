#include "manifest.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

#include "apk/resource_values.hpp"

namespace dex::apk {

namespace {

/** A framework attribute that the manifest view reads: its name in the
 *  android namespace, and its resource id. */
struct FrameworkAttribute
{
    std::string_view name;
    uint32_t id;
};

/** The framework attributes the manifest view reads.  The resource ids are
 *  part of Android's public API and never change.  They were taken from the
 *  Android SDK with `aapt2 dump resources platforms/android-34/android.jar`
 *  (android-37 gives the same values). */
namespace framework {
constexpr FrameworkAttribute kName{"name", 0x01010003};
constexpr FrameworkAttribute kPermission{"permission", 0x01010006};
constexpr FrameworkAttribute kProtectionLevel{"protectionLevel", 0x01010009};
constexpr FrameworkAttribute kDebuggable{"debuggable", 0x0101000f};
constexpr FrameworkAttribute kExported{"exported", 0x01010010};
constexpr FrameworkAttribute kMinSdkVersion{"minSdkVersion", 0x0101020c};
constexpr FrameworkAttribute kVersionCode{"versionCode", 0x0101021b};
constexpr FrameworkAttribute kVersionName{"versionName", 0x0101021c};
constexpr FrameworkAttribute kTargetSdkVersion{"targetSdkVersion", 0x01010270};
constexpr FrameworkAttribute kAllowBackup{"allowBackup", 0x01010280};
} // namespace framework

constexpr std::string_view kAndroidNamespace = "http://schemas.android.com/apk/res/android";

using detail::FinalValue;
using detail::ResourceTableAccess;

/** A boolean or integer attribute as read. */
template <typename T> struct TypedRead
{
    /** The value written, or for a reference the default configuration's
     *  value; nullopt when absent or unknown. */
    std::optional<T> value;
    /** The reference id when the attribute is written as a reference. */
    std::optional<uint32_t> reference;
    /** Every distinct value the attribute takes over all configurations;
     *  nullopt stands for a configuration whose value is unknown.  Empty for
     *  a missing attribute. */
    std::vector<std::optional<T>> all;

    bool varies() const { return all.size() > 1; }
    void add(std::optional<T> v)
    {
        if (std::find(all.begin(), all.end(), v) == all.end())
            all.push_back(v);
    }
};

/** A text attribute as read: its value when it has a single one, and its
 *  reference id when it is written as a reference. */
struct TextRead
{
    std::optional<std::string> value;
    std::optional<uint32_t> reference;
};

/** Finds framework attributes on an element the way Android does, and reads
 *  their values the way Android's package parser reads them.
 *
 *  Android reads a framework attribute by its resource id: the document's
 *  resource map gives each attribute name string an id, and only the id counts
 *  (AssetManager matches the ids of the attributes it asks for).  So an
 *  attribute whose name string was changed is still read, and an attribute
 *  that is only named like a framework attribute (no id, or another id) is
 *  not.  dexpp does the same whenever the document has a resource map, as
 *  every manifest aapt or aapt2 writes does.  When an id appears twice, the
 *  first attribute wins.
 *
 *  A document without any resource map was not written by aapt or aapt2, and
 *  Android would find no framework attribute in it.  For such a document dexpp
 *  falls back to the attribute's name in the android namespace, so a manifest
 *  made by another tool still reads as written.
 *
 *  The package parser (frameworks/base, ParsingPackageUtils and the
 *  Parsed*Utils classes) reads the attributes through a TypedArray, which
 *  resolves references with the AssetManager.  The functions below follow
 *  the TypedArray method it uses for each kind of value. */
class AttributeLookup
{
    // How many resource values one search looks at, at most.  A real
    // resource has at most a few hundred configurations (one per language).
    static constexpr std::size_t kSearchLimit = 4096;
    // How many values all the searches of one manifest look at, at most.
    // Past it, every further reference reads as unknown, which is the safe
    // answer.  This stops a hostile manifest that refers many times to
    // resources with huge reference graphs.
    static constexpr std::size_t kManifestLimit = std::size_t{1} << 20;

  public:
    AttributeLookup(bool by_resource_id, const ResourceTable *resources)
        : by_resource_id_(by_resource_id), resources_(resources)
    {
    }

    const axml::XmlAttribute *find(const axml::XmlNode &node,
                                   const FrameworkAttribute &attribute) const
    {
        if (by_resource_id_)
            return node.attribute_by_id(attribute.id);
        for (const auto &a : node.attributes)
            if (a.name == attribute.name && a.namespace_uri == kAndroidNamespace)
                return &a;
        return nullptr;
    }

    /** A boolean attribute, read like TypedArray.getBoolean(attribute,
     *  @p android_default): a literal value, or a reference resolved through
     *  the resource table in every configuration. */
    TypedRead<bool> boolean(const axml::XmlNode &node, const FrameworkAttribute &attribute,
                            bool android_default) const
    {
        TypedRead<bool> out;
        const axml::XmlAttribute *a = find(node, attribute);
        if (!a)
            return out;
        out.reference = reference_of(*a);
        if (!out.reference.has_value()) {
            // A null value counts as not written: TypedArray.hasValue() is
            // false for it.
            if (a->typed.type == detail::kTypeNull)
                return out;
            std::optional<std::string_view> text;
            if (a->typed.type == detail::kTypeString)
                text = a->typed_string;
            out.value =
                detail::boolean_value(a->typed.type, a->typed.data, text).value_or(android_default);
            out.add(out.value);
            return out;
        }
        if (!resources_ || !detail::is_resource_reference(a->typed.type)) {
            out.add(std::nullopt);
            return out;
        }
        auto outcome = [&](const FinalValue &v) -> std::optional<bool> {
            // A null value or a theme attribute at the end of the chain:
            // unknown.
            if (v.type == detail::kTypeNull || v.type == detail::kTypeAttribute ||
                v.type == detail::kTypeDynamicAttribute)
                return std::nullopt;
            std::optional<std::string_view> text;
            if (v.type == detail::kTypeString)
                text = ResourceTableAccess::pool_string(*resources_, v.data);
            return detail::boolean_value(v.type, v.data, text).value_or(android_default);
        };
        if (auto v = ResourceTableAccess::default_value(*resources_, *out.reference))
            out.value = outcome(*v);
        collect(out, outcome);
        return out;
    }

    /** An integer attribute: a literal value, or a reference resolved
     *  through the resource table in every configuration. */
    TypedRead<int64_t> integer(const axml::XmlNode &node, const FrameworkAttribute &attribute) const
    {
        TypedRead<int64_t> out;
        const axml::XmlAttribute *a = find(node, attribute);
        if (!a)
            return out;
        out.reference = reference_of(*a);
        if (!out.reference.has_value()) {
            out.value = a->as_int();
            if (out.value.has_value())
                out.add(out.value);
            return out;
        }
        if (!resources_ || !detail::is_resource_reference(a->typed.type)) {
            out.add(std::nullopt);
            return out;
        }
        auto outcome = [](const FinalValue &v) -> std::optional<int64_t> {
            if (!detail::is_int_type(v.type))
                return std::nullopt;
            return static_cast<int32_t>(v.data);
        };
        if (auto v = ResourceTableAccess::default_value(*resources_, *out.reference))
            out.value = outcome(*v);
        collect(out, outcome);
        return out;
    }

    /** A text attribute that the package parser reads with
     *  TypedArray.getNonConfigurationString(attribute, 0): android:name of a
     *  component or of a `<permission>`, android:permission and
     *  android:versionName.
     *
     *  A literal is the string the typed value names.  A reference is
     *  resolved, but Android returns null when the string resource changes
     *  with the configuration (see detail::kNonConfigurationMask), so that
     *  case has no value.  A string that differs only between Android
     *  versions passes that check, but its value depends on the device, so
     *  it has no single value either. */
    TextRead text(const axml::XmlNode &node, const FrameworkAttribute &attribute) const
    {
        TextRead out;
        const axml::XmlAttribute *a = find(node, attribute);
        if (!a)
            return out;
        out.reference = reference_of(*a);
        if (!out.reference.has_value()) {
            if (a->typed.type == detail::kTypeString)
                out.value = a->typed_string;
            return out;
        }
        if (!resources_ || !detail::is_resource_reference(a->typed.type))
            return out;
        const detail::EveryValue &every = every_value(*out.reference);
        if (every.incomplete || (every.spec_flags & detail::kNonConfigurationMask) != 0)
            return out;
        std::optional<std::string_view> single;
        for (const FinalValue &v : every.values) {
            if (v.type != detail::kTypeString)
                return out;
            std::optional<std::string_view> s =
                ResourceTableAccess::pool_string(*resources_, v.data);
            if (!s.has_value() || (single.has_value() && *single != *s))
                return out;
            single = s;
        }
        if (single.has_value())
            out.value = std::string(*single);
        return out;
    }

    /** A text attribute that the package parser reads with
     *  TypedArray.getNonResourceString(): the name of a
     *  `<uses-permission>`.  Only a literal string counts; Android ignores a
     *  reference ("don't allow this value to be a reference to a resource
     *  that may change", ParsingPackageUtils). */
    std::optional<std::string> literal_text(const axml::XmlNode &node,
                                            const FrameworkAttribute &attribute) const
    {
        const axml::XmlAttribute *a = find(node, attribute);
        if (a && a->typed.type == detail::kTypeString)
            return a->typed_string;
        return std::nullopt;
    }

    /** The android:name of an `<action>` or `<category>`.  The package
     *  parser reads it with XmlPullParser.getAttributeValue(), which gives
     *  the raw text and never resolves a reference.  "" when there is
     *  none. */
    std::string raw_name_of(const axml::XmlNode &node) const
    {
        if (const auto *a = find(node, framework::kName))
            if (auto s = a->as_string())
                return std::string(*s);
        return std::string();
    }

  private:
    // Res_value data types of the references an attribute can hold
    // (ResourceTypes.h).  A resource reference ("@bool/x") names a resource;
    // an attribute reference ("?attr/x") names a theme attribute, whose value
    // depends on the theme the app runs with.  The dynamic forms are the same
    // references into a shared library's resources.

    /** The id a reference-typed value holds; nullopt for any other value. */
    static std::optional<uint32_t> reference_of(const axml::XmlAttribute &a)
    {
        const uint8_t type = a.typed.type;
        if (type == detail::kTypeReference || type == detail::kTypeAttribute ||
            type == detail::kTypeDynamicReference || type == detail::kTypeDynamicAttribute)
            return a.typed.data;
        return std::nullopt;
    }

    /** Every value @p id can end at (see ResourceTableAccess::every_value),
     *  searched once per id and within the manifest's budget. */
    const detail::EveryValue &every_value(uint32_t id) const
    {
        auto it = searches_.find(id);
        if (it != searches_.end())
            return it->second;
        detail::EveryValue every;
        if (budget_ == 0)
            every.incomplete = true;
        else
            every =
                ResourceTableAccess::every_value(*resources_, id, std::min(kSearchLimit, budget_));
        budget_ -= std::min(budget_, every.visited);
        return searches_.emplace(id, std::move(every)).first->second;
    }

    /** Fill out.all with the outcome of every value the reference can end
     *  at, plus the default configuration's.  An incomplete search adds an
     *  unknown value. */
    template <typename T, typename Outcome> void collect(TypedRead<T> &out, Outcome outcome) const
    {
        const detail::EveryValue &every = every_value(*out.reference);
        out.add(out.value);
        for (const FinalValue &v : every.values)
            out.add(outcome(v));
        if (every.incomplete)
            out.add(std::nullopt);
    }

    bool by_resource_id_;
    const ResourceTable *resources_;
    mutable std::size_t budget_ = kManifestLimit;
    mutable std::unordered_map<uint32_t, detail::EveryValue> searches_;
};

// android:name on a component may be a leaf (".Foo") or partial ("sub.Foo");
// qualify it against the package, matching the Android runtime's rules.
std::string qualify(std::string_view name, std::string_view package)
{
    if (name.empty())
        return std::string(name);
    if (name.front() == '.')
        return std::string(package) + std::string(name);
    if (name.find('.') == std::string_view::npos)
        return std::string(package) + "." + std::string(name);
    return std::string(name);
}

IntentFilter parse_filter(const AttributeLookup &attrs, const axml::XmlNode &node)
{
    IntentFilter f;
    for (const auto *action : node.children_named("action"))
        f.actions.push_back(attrs.raw_name_of(*action));
    for (const auto *cat : node.children_named("category"))
        f.categories.push_back(attrs.raw_name_of(*cat));
    return f;
}

void collect_components(const AttributeLookup &attrs, const axml::XmlNode &app,
                        std::string_view package, std::vector<Component> &out)
{
    struct Tag
    {
        const char *tag;
        Component::Kind kind;
    };
    constexpr Tag kinds[] = {
        {"activity", Component::Kind::Activity},
        {"service", Component::Kind::Service},
        {"receiver", Component::Kind::Receiver},
        {"provider", Component::Kind::Provider},
    };

    for (const auto &tag : kinds) {
        for (const auto *node : app.children_named(tag.tag)) {
            Component c;
            c.kind = tag.kind;
            auto name = attrs.text(*node, framework::kName);
            c.name = qualify(name.value.value_or(std::string()), package);
            c.name_reference = name.reference;
            // The parser reads android:exported only when it has a value
            // (TypedArray.hasValue), and then with getBoolean(attr, false).
            auto exported = attrs.boolean(*node, framework::kExported, false);
            c.exported = exported.value;
            c.exported_reference = exported.reference;
            c.exported_varies = exported.varies();
            auto permission = attrs.text(*node, framework::kPermission);
            c.permission = std::move(permission.value);
            c.permission_reference = permission.reference;
            for (const auto *filter : node->children_named("intent-filter"))
                c.intent_filters.push_back(parse_filter(attrs, *filter));
            out.push_back(std::move(c));
        }
    }
}

} // namespace

Manifest::Manifest(axml::XmlDocument doc, const ResourceTable *resources) : doc_(std::move(doc))
{
    const axml::XmlNode &root = doc_.root;
    const AttributeLookup attrs(doc_.has_resource_map, resources);

    // `package` is not a framework attribute: it has no namespace and no
    // resource id, and Android reads it by name.
    for (const auto &a : root.attributes)
        if (a.name == "package" && a.namespace_uri.empty()) {
            if (auto s = a.as_string())
                package_ = std::string(*s);
            break;
        }

    auto version_code = attrs.integer(root, framework::kVersionCode);
    version_code_ = version_code.value;
    version_code_reference_ = version_code.reference;
    auto version_name = attrs.text(root, framework::kVersionName);
    version_name_ = std::move(version_name.value);
    version_name_reference_ = version_name.reference;

    if (const auto *uses_sdk = root.child("uses-sdk")) {
        auto min_sdk = attrs.integer(*uses_sdk, framework::kMinSdkVersion);
        min_sdk_ = min_sdk.value;
        min_sdk_reference_ = min_sdk.reference;
        auto target_sdk = attrs.integer(*uses_sdk, framework::kTargetSdkVersion);
        target_sdk_ = target_sdk.value;
        target_sdk_reference_ = target_sdk.reference;
    }

    for (const auto *up : root.children_named("uses-permission")) {
        std::string n = attrs.literal_text(*up, framework::kName).value_or(std::string());
        if (!n.empty())
            permissions_.push_back(std::move(n));
    }

    // signatureOrSystem is deprecated but still appears; both 2 and 3 mean the
    // permission requires the app's signing key (or system), so a third-party
    // app cannot hold it. The base level is the low nibble; higher bits are flags.
    constexpr int kProtectionMaskBase = 0x0f;
    constexpr int kProtectionSignature = 2;
    constexpr int kProtectionSignatureOrSystem = 3;
    auto is_signature = [&](const std::optional<int64_t> &level) {
        if (!level.has_value())
            return false;
        const int base = static_cast<int>(*level) & kProtectionMaskBase;
        return base == kProtectionSignature || base == kProtectionSignatureOrSystem;
    };

    for (const auto *dp : root.children_named("permission")) {
        std::string n = attrs.text(*dp, framework::kName).value.value_or(std::string());
        if (n.empty())
            continue;
        // protectionLevel defaults to "normal" (0) when omitted, and is
        // unknown (nullopt) when it is a reference that could not be resolved.
        auto level = attrs.integer(*dp, framework::kProtectionLevel);
        ProtectionLevel known;
        if (!level.value.has_value() && !level.reference.has_value()) {
            known.value = 0;
        }
        else {
            if (level.value.has_value())
                known.value = static_cast<int>(*level.value);
            known.varies = level.varies();
            // The guard counts only when every configuration gives a
            // signature level: on a device that matches a weaker one, a
            // third-party app can hold the permission.
            known.always_signature =
                !level.all.empty() && std::all_of(level.all.begin(), level.all.end(), is_signature);
        }
        declared_permission_levels_[n] = known;
        declared_permissions_.push_back(std::move(n));
    }

    if (const auto *app = root.child("application")) {
        // ParsingPackageUtils reads both with TypedArray.getBoolean:
        // debuggable defaults to false, allowBackup to true.
        auto debuggable = attrs.boolean(*app, framework::kDebuggable, false);
        debuggable_ = debuggable.value;
        debuggable_reference_ = debuggable.reference;
        debuggable_varies_ = debuggable.varies();
        auto allow_backup = attrs.boolean(*app, framework::kAllowBackup, true);
        allow_backup_ = allow_backup.value;
        allow_backup_reference_ = allow_backup.reference;
        allow_backup_varies_ = allow_backup.varies();
        collect_components(attrs, *app, package_, components_);
    }

    // Resolve each component's guard against the app-declared permission levels.
    for (auto &c : components_) {
        if (!c.permission.has_value())
            continue; // none, or a reference without a single value.
        auto it = declared_permission_levels_.find(*c.permission);
        if (it == declared_permission_levels_.end())
            continue; // undeclared (e.g. platform) permission — treated as reachable.
        // A level that is unknown, or below signature in some configuration,
        // does not guard.
        c.guarded_by_signature_permission = it->second.always_signature;
    }
}

std::optional<int> Manifest::declared_permission_protection_level(std::string_view name) const
{
    auto it = declared_permission_levels_.find(std::string(name));
    if (it == declared_permission_levels_.end())
        return std::nullopt;
    return it->second.value; // nullopt when the level is an unresolved reference
}

bool Manifest::declared_permission_protection_level_varies(std::string_view name) const
{
    auto it = declared_permission_levels_.find(std::string(name));
    return it != declared_permission_levels_.end() && it->second.varies;
}

std::optional<std::string_view> Manifest::version_name() const
{
    if (version_name_.has_value())
        return *version_name_;
    return std::nullopt;
}

std::vector<const Component *> Manifest::components_of(Component::Kind kind) const
{
    std::vector<const Component *> out;
    for (const auto &c : components_)
        if (c.kind == kind)
            out.push_back(&c);
    return out;
}

std::optional<std::string_view> Manifest::launcher_activity() const
{
    for (const auto &c : components_) {
        if (c.kind != Component::Kind::Activity)
            continue;
        for (const auto &f : c.intent_filters)
            if (f.is_launcher())
                return c.name;
    }
    return std::nullopt;
}

} // namespace dex::apk
