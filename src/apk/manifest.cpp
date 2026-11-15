#include "manifest.hpp"

#include <cstdint>
#include <utility>

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

/** A boolean or integer attribute as read: its value when known, and its
 *  reference id when it is written as a reference. */
template <typename T> struct TypedRead
{
    std::optional<T> value;
    std::optional<uint32_t> reference;
};

/** Finds framework attributes on an element the way Android does.
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
 *  made by another tool still reads as written. */
class AttributeLookup
{
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

    /** A boolean attribute: a literal value, or a reference resolved
     *  through the resource table. */
    TypedRead<bool> boolean(const axml::XmlNode &node, const FrameworkAttribute &attribute) const
    {
        TypedRead<bool> out;
        if (const auto *a = find(node, attribute)) {
            out.value = a->as_bool();
            if (!out.value.has_value()) {
                out.reference = reference_of(*a);
                if (out.reference.has_value() && resources_ && is_resource_reference(*a))
                    out.value = resources_->resolve_bool(*out.reference);
            }
        }
        return out;
    }

    /** An integer attribute: a literal value, or a reference resolved
     *  through the resource table. */
    TypedRead<int64_t> integer(const axml::XmlNode &node, const FrameworkAttribute &attribute) const
    {
        TypedRead<int64_t> out;
        if (const auto *a = find(node, attribute)) {
            out.value = a->as_int();
            if (!out.value.has_value()) {
                out.reference = reference_of(*a);
                if (out.reference.has_value() && resources_ && is_resource_reference(*a))
                    out.value = resources_->resolve_int(*out.reference);
            }
        }
        return out;
    }

    std::optional<std::string> text(const axml::XmlNode &node,
                                    const FrameworkAttribute &attribute) const
    {
        if (const auto *a = find(node, attribute))
            if (auto s = a->as_string())
                return std::string(*s);
        return std::nullopt;
    }

    /** The element's android:name, or "" when it has none. */
    std::string name_of(const axml::XmlNode &node) const
    {
        return text(node, framework::kName).value_or(std::string());
    }

  private:
    // Res_value data types of the references an attribute can hold
    // (ResourceTypes.h).  A resource reference ("@bool/x") names a resource;
    // an attribute reference ("?attr/x") names a theme attribute, whose value
    // depends on the theme the app runs with.  The dynamic forms are the same
    // references into a shared library's resources.
    static constexpr uint8_t kReference = 0x01;
    static constexpr uint8_t kAttribute = 0x02;
    static constexpr uint8_t kDynamicReference = 0x07;
    static constexpr uint8_t kDynamicAttribute = 0x08;

    /** The id a reference-typed value holds; nullopt for any other value. */
    static std::optional<uint32_t> reference_of(const axml::XmlAttribute &a)
    {
        const uint8_t type = a.typed.type;
        if (type == kReference || type == kAttribute || type == kDynamicReference ||
            type == kDynamicAttribute)
            return a.typed.data;
        return std::nullopt;
    }

    /** True for a reference to a resource, which the resource table can
     *  resolve; a theme attribute cannot be resolved without a theme. */
    static bool is_resource_reference(const axml::XmlAttribute &a)
    {
        return a.typed.type == kReference || a.typed.type == kDynamicReference;
    }

    bool by_resource_id_;
    const ResourceTable *resources_;
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
        f.actions.push_back(attrs.name_of(*action));
    for (const auto *cat : node.children_named("category"))
        f.categories.push_back(attrs.name_of(*cat));
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
            c.name = qualify(attrs.name_of(*node), package);
            auto exported = attrs.boolean(*node, framework::kExported);
            c.exported = exported.value;
            c.exported_reference = exported.reference;
            c.permission = attrs.text(*node, framework::kPermission);
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
    version_name_ = attrs.text(root, framework::kVersionName);

    if (const auto *uses_sdk = root.child("uses-sdk")) {
        auto min_sdk = attrs.integer(*uses_sdk, framework::kMinSdkVersion);
        min_sdk_ = min_sdk.value;
        min_sdk_reference_ = min_sdk.reference;
        auto target_sdk = attrs.integer(*uses_sdk, framework::kTargetSdkVersion);
        target_sdk_ = target_sdk.value;
        target_sdk_reference_ = target_sdk.reference;
    }

    for (const auto *up : root.children_named("uses-permission")) {
        std::string n = attrs.name_of(*up);
        if (!n.empty())
            permissions_.push_back(std::move(n));
    }
    for (const auto *dp : root.children_named("permission")) {
        std::string n = attrs.name_of(*dp);
        if (n.empty())
            continue;
        // protectionLevel defaults to "normal" (0) when omitted, and is
        // unknown (nullopt) when it is a reference that could not be resolved.
        auto level = attrs.integer(*dp, framework::kProtectionLevel);
        std::optional<int> known = 0;
        if (level.value.has_value())
            known = static_cast<int>(*level.value);
        else if (level.reference.has_value())
            known = std::nullopt;
        declared_permission_levels_[n] = known;
        declared_permissions_.push_back(std::move(n));
    }

    if (const auto *app = root.child("application")) {
        auto debuggable = attrs.boolean(*app, framework::kDebuggable);
        debuggable_ = debuggable.value;
        debuggable_reference_ = debuggable.reference;
        auto allow_backup = attrs.boolean(*app, framework::kAllowBackup);
        allow_backup_ = allow_backup.value;
        allow_backup_reference_ = allow_backup.reference;
        collect_components(attrs, *app, package_, components_);
    }

    // Resolve each component's guard against the app-declared permission levels.
    // signatureOrSystem is deprecated but still appears; both 2 and 3 mean the
    // permission requires the app's signing key (or system), so a third-party
    // app cannot hold it. The base level is the low nibble; higher bits are flags.
    constexpr int kProtectionMaskBase = 0x0f;
    constexpr int kProtectionSignature = 2;
    constexpr int kProtectionSignatureOrSystem = 3;
    for (auto &c : components_) {
        if (!c.permission.has_value())
            continue;
        auto it = declared_permission_levels_.find(*c.permission);
        if (it == declared_permission_levels_.end())
            continue; // undeclared (e.g. platform) permission — treated as reachable.
        if (!it->second.has_value())
            continue; // level unknown (unresolved reference) — treated as reachable.
        int base = *it->second & kProtectionMaskBase;
        if (base == kProtectionSignature || base == kProtectionSignatureOrSystem)
            c.guarded_by_signature_permission = true;
    }
}

std::optional<int> Manifest::declared_permission_protection_level(std::string_view name) const
{
    auto it = declared_permission_levels_.find(std::string(name));
    if (it == declared_permission_levels_.end())
        return std::nullopt;
    return it->second; // nullopt when the level is an unresolved reference
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
