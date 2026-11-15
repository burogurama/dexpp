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
    explicit AttributeLookup(bool by_resource_id) : by_resource_id_(by_resource_id) {}

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

    std::optional<bool> boolean(const axml::XmlNode &node,
                                const FrameworkAttribute &attribute) const
    {
        if (const auto *a = find(node, attribute))
            return a->as_bool();
        return std::nullopt;
    }

    std::optional<int64_t> integer(const axml::XmlNode &node,
                                   const FrameworkAttribute &attribute) const
    {
        if (const auto *a = find(node, attribute))
            return a->as_int();
        return std::nullopt;
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
    bool by_resource_id_;
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
            c.exported = attrs.boolean(*node, framework::kExported);
            c.permission = attrs.text(*node, framework::kPermission);
            for (const auto *filter : node->children_named("intent-filter"))
                c.intent_filters.push_back(parse_filter(attrs, *filter));
            out.push_back(std::move(c));
        }
    }
}

} // namespace

Manifest::Manifest(axml::XmlDocument doc) : doc_(std::move(doc))
{
    const axml::XmlNode &root = doc_.root;
    const AttributeLookup attrs(doc_.has_resource_map);

    // `package` is not a framework attribute: it has no namespace and no
    // resource id, and Android reads it by name.
    for (const auto &a : root.attributes)
        if (a.name == "package" && a.namespace_uri.empty()) {
            if (auto s = a.as_string())
                package_ = std::string(*s);
            break;
        }

    version_code_ = attrs.integer(root, framework::kVersionCode);
    version_name_ = attrs.text(root, framework::kVersionName);

    if (const auto *uses_sdk = root.child("uses-sdk")) {
        min_sdk_ = attrs.integer(*uses_sdk, framework::kMinSdkVersion);
        target_sdk_ = attrs.integer(*uses_sdk, framework::kTargetSdkVersion);
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
        // protectionLevel defaults to "normal" (0) when omitted.
        int level = static_cast<int>(attrs.integer(*dp, framework::kProtectionLevel).value_or(0));
        declared_permission_levels_[n] = level;
        declared_permissions_.push_back(std::move(n));
    }

    if (const auto *app = root.child("application")) {
        debuggable_ = attrs.boolean(*app, framework::kDebuggable);
        allow_backup_ = attrs.boolean(*app, framework::kAllowBackup);
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
        int base = it->second & kProtectionMaskBase;
        if (base == kProtectionSignature || base == kProtectionSignatureOrSystem)
            c.guarded_by_signature_permission = true;
    }
}

std::optional<int> Manifest::declared_permission_protection_level(std::string_view name) const
{
    auto it = declared_permission_levels_.find(std::string(name));
    if (it == declared_permission_levels_.end())
        return std::nullopt;
    return it->second;
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
