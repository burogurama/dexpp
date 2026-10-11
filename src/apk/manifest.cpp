#include "manifest.hpp"

#include <utility>

namespace dex::apk {

namespace {

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

std::optional<bool> attr_bool(const axml::XmlNode &node, std::string_view name)
{
    if (const auto *a = node.attribute(name))
        return a->as_bool();
    return std::nullopt;
}

std::optional<int64_t> attr_int(const axml::XmlNode &node, std::string_view name)
{
    if (const auto *a = node.attribute(name))
        return a->as_int();
    return std::nullopt;
}

std::string attr_name_of(const axml::XmlNode &node)
{
    if (const auto *a = node.attribute("name"))
        if (auto s = a->as_string())
            return std::string(*s);
    return {};
}

IntentFilter parse_filter(const axml::XmlNode &node)
{
    IntentFilter f;
    for (const auto *action : node.children_named("action"))
        f.actions.push_back(attr_name_of(*action));
    for (const auto *cat : node.children_named("category"))
        f.categories.push_back(attr_name_of(*cat));
    return f;
}

void collect_components(const axml::XmlNode &app, std::string_view package,
                        std::vector<Component> &out)
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
            c.name = qualify(attr_name_of(*node), package);
            c.exported = attr_bool(*node, "exported");
            if (const auto *perm = node->attribute("permission"))
                if (auto s = perm->as_string())
                    c.permission = std::string(*s);
            for (const auto *filter : node->children_named("intent-filter"))
                c.intent_filters.push_back(parse_filter(*filter));
            out.push_back(std::move(c));
        }
    }
}

} // namespace

Manifest::Manifest(axml::XmlDocument doc) : doc_(std::move(doc))
{
    const axml::XmlNode &root = doc_.root;

    if (const auto *p = root.attribute("package"))
        if (auto s = p->as_string())
            package_ = std::string(*s);

    version_code_ = attr_int(root, "versionCode");
    if (const auto *vn = root.attribute("versionName"))
        if (auto s = vn->as_string())
            version_name_ = std::string(*s);

    if (const auto *uses_sdk = root.child("uses-sdk")) {
        min_sdk_ = attr_int(*uses_sdk, "minSdkVersion");
        target_sdk_ = attr_int(*uses_sdk, "targetSdkVersion");
    }

    for (const auto *up : root.children_named("uses-permission")) {
        std::string n = attr_name_of(*up);
        if (!n.empty())
            permissions_.push_back(std::move(n));
    }
    for (const auto *dp : root.children_named("permission")) {
        std::string n = attr_name_of(*dp);
        if (n.empty())
            continue;
        // protectionLevel defaults to "normal" (0) when omitted.
        int level = static_cast<int>(attr_int(*dp, "protectionLevel").value_or(0));
        declared_permission_levels_[n] = level;
        declared_permissions_.push_back(std::move(n));
    }

    if (const auto *app = root.child("application")) {
        debuggable_ = attr_bool(*app, "debuggable");
        allow_backup_ = attr_bool(*app, "allowBackup");
        collect_components(*app, package_, components_);
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
