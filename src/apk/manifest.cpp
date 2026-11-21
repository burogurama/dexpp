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
constexpr FrameworkAttribute kReadPermission{"readPermission", 0x01010007};
constexpr FrameworkAttribute kWritePermission{"writePermission", 0x01010008};
constexpr FrameworkAttribute kProtectionLevel{"protectionLevel", 0x01010009};
constexpr FrameworkAttribute kDebuggable{"debuggable", 0x0101000f};
constexpr FrameworkAttribute kExported{"exported", 0x01010010};
constexpr FrameworkAttribute kPath{"path", 0x0101002a};
constexpr FrameworkAttribute kPathPrefix{"pathPrefix", 0x0101002b};
constexpr FrameworkAttribute kPathPattern{"pathPattern", 0x0101002c};
constexpr FrameworkAttribute kTargetActivity{"targetActivity", 0x01010202};
constexpr FrameworkAttribute kMinSdkVersion{"minSdkVersion", 0x0101020c};
constexpr FrameworkAttribute kVersionCode{"versionCode", 0x0101021b};
constexpr FrameworkAttribute kVersionName{"versionName", 0x0101021c};
constexpr FrameworkAttribute kTargetSdkVersion{"targetSdkVersion", 0x01010270};
constexpr FrameworkAttribute kAllowBackup{"allowBackup", 0x01010280};
constexpr FrameworkAttribute kPathSuffix{"pathSuffix", 0x0101061e};
constexpr FrameworkAttribute kPathAdvancedPattern{"pathAdvancedPattern", 0x01010620};
} // namespace framework

constexpr std::string_view kAndroidNamespace = "http://schemas.android.com/apk/res/android";

using detail::FinalValue;
using detail::ResourceTableAccess;

/** What Android reads from an attribute in one configuration. */
template <typename T> struct Reading
{
    enum class Kind : uint8_t {
        Value,   ///< `value`.
        Null,    ///< A null value: Android reads it as not written.
        Unknown, ///< dexpp cannot tell.
    };
    Kind kind = Kind::Null;
    T value{};

    static Reading of(T v) { return Reading{Kind::Value, std::move(v)}; }
    static Reading null() { return Reading{Kind::Null, T{}}; }
    static Reading unknown() { return Reading{Kind::Unknown, T{}}; }
    bool operator==(const Reading &) const = default;
};

/** An attribute as read. */
template <typename T> struct AttributeRead
{
    /** The element has the attribute. */
    bool present = false;
    /** The reference id when the attribute is written as a reference. */
    std::optional<uint32_t> reference;
    /** What Android reads in the default configuration; for text, the one
     *  value Android reads on every device.  Null for a missing attribute. */
    Reading<T> first;
    /** Every distinct reading over all configurations.  Empty for a missing
     *  attribute. */
    std::vector<Reading<T>> all;

    std::optional<T> value() const
    {
        if (first.kind == Reading<T>::Kind::Value)
            return first.value;
        return std::nullopt;
    }
    bool unknown() const { return first.kind == Reading<T>::Kind::Unknown; }
    /** The attribute gives no value: it is missing, or Android reads it as
     *  not written. */
    bool gives_nothing() const { return first.kind == Reading<T>::Kind::Null; }

    void add(const Reading<T> &r)
    {
        if (std::find(all.begin(), all.end(), r) == all.end())
            all.push_back(r);
    }
    /** True when Android reads different values in different
     *  configurations; a null reading counts as @p if_null, what Android
     *  uses when the attribute is not written (unknown when nullopt), and an
     *  unknown one as one more answer. */
    bool varies(const std::optional<T> &if_null) const
    {
        std::vector<Reading<T>> seen;
        for (Reading<T> r : all) {
            if (r.kind == Reading<T>::Kind::Null)
                r = if_null.has_value() ? Reading<T>::of(*if_null) : Reading<T>::unknown();
            if (std::find(seen.begin(), seen.end(), r) == seen.end())
                seen.push_back(r);
        }
        return seen.size() > 1;
    }
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
     *  the resource table in every configuration.  A null value reads as
     *  Null: TypedArray.hasValue() is false for it, and getBoolean() gives
     *  the default.  With no @p android_default, a value that gives the
     *  default (empty text) reads as Null too: the caller decides what the
     *  default is. */
    AttributeRead<bool> boolean(const axml::XmlNode &node, const FrameworkAttribute &attribute,
                                std::optional<bool> android_default) const
    {
        auto outcome = [&](uint8_t type, uint32_t data,
                           std::optional<std::string_view> text) -> Reading<bool> {
            if (detail::is_null_value(type, data))
                return Reading<bool>::null();
            // A theme attribute: its value depends on the theme.
            if (type == detail::kTypeAttribute || type == detail::kTypeDynamicAttribute)
                return Reading<bool>::unknown();
            const std::optional<bool> value = detail::boolean_value(type, data, text);
            if (value.has_value())
                return Reading<bool>::of(*value);
            if (android_default.has_value())
                return Reading<bool>::of(*android_default);
            return Reading<bool>::null();
        };
        auto literal = [&](const axml::XmlAttribute &a) {
            std::optional<std::string_view> text;
            if (a.typed.type == detail::kTypeString)
                text = a.typed_string;
            return outcome(a.typed.type, a.typed.data, text);
        };
        auto resolved = [&](const FinalValue &v) {
            std::optional<std::string_view> text;
            if (v.type == detail::kTypeString)
                text = ResourceTableAccess::pool_string(*resources_, v.data);
            return outcome(v.type, v.data, text);
        };
        return read<bool>(node, attribute, literal, resolved);
    }

    /** An integer attribute: a literal value, or a reference resolved
     *  through the resource table in every configuration.  Android reads
     *  these with TypedArray.getInt() or peekValue(), which give the default
     *  or nothing for a null value. */
    AttributeRead<int64_t> integer(const axml::XmlNode &node,
                                   const FrameworkAttribute &attribute) const
    {
        auto outcome = [](uint8_t type, uint32_t data) -> Reading<int64_t> {
            if (detail::is_null_value(type, data))
                return Reading<int64_t>::null();
            if (!detail::is_int_type(type))
                return Reading<int64_t>::unknown();
            return Reading<int64_t>::of(static_cast<int32_t>(data));
        };
        auto literal = [&](const axml::XmlAttribute &a) {
            return outcome(a.typed.type, a.typed.data);
        };
        auto resolved = [&](const FinalValue &v) { return outcome(v.type, v.data); };
        return read<int64_t>(node, attribute, literal, resolved);
    }

    /** A text attribute that the package parser reads with
     *  TypedArray.getNonConfigurationString(attribute, 0): android:name of a
     *  component or of a `<permission>`, android:permission,
     *  android:readPermission, android:writePermission and
     *  android:versionName.
     *
     *  A literal is the string the typed value names.  A reference is
     *  resolved, but Android returns null when the string resource changes
     *  with the configuration (see detail::kNonConfigurationMask), so that
     *  case reads as Null.  A string that differs only between Android
     *  versions passes that check, but its value depends on the device, so
     *  it reads as Unknown.  A null value is Null, as getNonConfigurationString
     *  returns null for it. */
    AttributeRead<std::string> text(const axml::XmlNode &node,
                                    const FrameworkAttribute &attribute) const
    {
        AttributeRead<std::string> out;
        const axml::XmlAttribute *a = find(node, attribute);
        if (!a)
            return out;
        out.present = true;
        out.reference = reference_of(*a);
        if (!out.reference.has_value()) {
            out.first = text_outcome(a->typed.type, a->typed.data, a->typed_string);
        }
        else if (!resources_ || !detail::is_resource_reference(a->typed.type)) {
            out.first = Reading<std::string>::unknown();
        }
        else {
            const detail::EveryValue &every = every_value(*out.reference);
            if (every.incomplete) {
                out.first = Reading<std::string>::unknown();
            }
            else if ((every.spec_flags & detail::kNonConfigurationMask) != 0) {
                out.first = Reading<std::string>::null(); // Android ignores it
            }
            else {
                // Every configuration must give the same reading; otherwise
                // the value depends on the device.
                std::optional<Reading<std::string>> single;
                for (const FinalValue &v : every.values) {
                    std::optional<std::string_view> s;
                    if (v.type == detail::kTypeString)
                        s = ResourceTableAccess::pool_string(*resources_, v.data);
                    Reading<std::string> r =
                        (v.type == detail::kTypeString && !s.has_value())
                            ? Reading<std::string>::unknown()
                            : text_outcome(v.type, v.data, s.value_or(std::string_view()));
                    if (single.has_value() && !(*single == r)) {
                        single = Reading<std::string>::unknown();
                        break;
                    }
                    single = std::move(r);
                }
                out.first = single.value_or(Reading<std::string>::unknown());
            }
        }
        out.add(out.first);
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

    /** The id a reference-typed value holds; nullopt for any other value,
     *  and for `@null` (a reference to id 0), which is a null value. */
    static std::optional<uint32_t> reference_of(const axml::XmlAttribute &a)
    {
        const uint8_t type = a.typed.type;
        if (detail::is_null_reference(type, a.typed.data))
            return std::nullopt;
        if (type == detail::kTypeReference || type == detail::kTypeAttribute ||
            type == detail::kTypeDynamicReference || type == detail::kTypeDynamicAttribute)
            return a.typed.data;
        return std::nullopt;
    }

    /** What getNonConfigurationString gives for a value that is not a
     *  reference: the string of a string value, null for a null value.  Any
     *  other type Android turns into text with TypedValue.coerceToString();
     *  dexpp does not, and reads it as unknown. */
    static Reading<std::string> text_outcome(uint8_t type, uint32_t data, std::string_view text)
    {
        if (detail::is_null_value(type, data))
            return Reading<std::string>::null();
        if (type == detail::kTypeString)
            return Reading<std::string>::of(std::string(text));
        return Reading<std::string>::unknown();
    }

    /** Read a boolean or integer attribute: @p literal gives the reading of
     *  a value that is not a reference, @p resolved the reading of a value
     *  a reference ends at.  The first reading is the default
     *  configuration's; `all` holds the reading of every configuration. */
    template <typename T, typename Literal, typename Resolved>
    AttributeRead<T> read(const axml::XmlNode &node, const FrameworkAttribute &attribute,
                          Literal literal, Resolved resolved) const
    {
        AttributeRead<T> out;
        const axml::XmlAttribute *a = find(node, attribute);
        if (!a)
            return out;
        out.present = true;
        out.reference = reference_of(*a);
        if (!out.reference.has_value()) {
            out.first = literal(*a);
            out.add(out.first);
            return out;
        }
        if (!resources_ || !detail::is_resource_reference(a->typed.type)) {
            out.first = Reading<T>::unknown();
            out.add(out.first);
            return out;
        }
        auto v = ResourceTableAccess::default_value(*resources_, *out.reference);
        out.first = v.has_value() ? resolved(*v) : Reading<T>::unknown();
        out.add(out.first);
        const detail::EveryValue &every = every_value(*out.reference);
        for (const FinalValue &final_value : every.values)
            out.add(resolved(final_value));
        if (every.incomplete)
            out.add(Reading<T>::unknown());
        return out;
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

/** A permission as Android ends up with it: a value, or unknown. */
struct Permission
{
    std::optional<std::string> value;
    bool unknown = false;

    /** What a text attribute gives when it gives a value or is unknown;
     *  @p otherwise when it gives none (missing, or read as not written). */
    static Permission of(const AttributeRead<std::string> &read, const Permission &otherwise)
    {
        if (read.gives_nothing())
            return otherwise;
        return Permission{read.value(), read.unknown()};
    }
    bool gives_something() const { return value.has_value() || unknown; }
};

/** The `<path-permission>` elements of a provider, read like
 *  ParsedProviderUtils.parsePathPermission. */
std::vector<PathPermission> path_permissions(const AttributeLookup &attrs,
                                             const axml::XmlNode &provider)
{
    // The path attributes in the order Android tries them: the first that
    // gives a value is the path.
    struct PathAttribute
    {
        const FrameworkAttribute &attribute;
        PathPermission::Match match;
    };
    const PathAttribute kPathAttributes[] = {
        {framework::kPathAdvancedPattern, PathPermission::Match::AdvancedPattern},
        {framework::kPathPattern, PathPermission::Match::Pattern},
        {framework::kPathPrefix, PathPermission::Match::Prefix},
        {framework::kPathSuffix, PathPermission::Match::Suffix},
        {framework::kPath, PathPermission::Match::Literal},
    };

    std::vector<PathPermission> out;
    for (const auto *node : provider.children_named("path-permission")) {
        // readPermission, else the element's permission; the same for
        // writePermission.  They never fall back to the provider's.
        const Permission own =
            Permission::of(attrs.text(*node, framework::kPermission), Permission{});
        const Permission read = Permission::of(attrs.text(*node, framework::kReadPermission), own);
        const Permission write =
            Permission::of(attrs.text(*node, framework::kWritePermission), own);
        // "No readPermission or writePermission for <path-permission>":
        // Android drops the element.
        if (!read.gives_something() && !write.gives_something())
            continue;

        PathPermission pp;
        bool has_path = false;
        for (const PathAttribute &candidate : kPathAttributes) {
            auto path = attrs.text(*node, candidate.attribute);
            if (path.gives_nothing())
                continue;
            pp.match = candidate.match;
            pp.path = path.value().value_or(std::string());
            pp.path_unknown = path.unknown();
            has_path = true;
            break;
        }
        // "No path, pathPrefix, or pathPattern for <path-permission>":
        // Android drops the element.
        if (!has_path)
            continue;

        pp.read_permission = read.value;
        pp.read_permission_unknown = read.unknown;
        pp.write_permission = write.value;
        pp.write_permission_unknown = write.unknown;
        out.push_back(std::move(pp));
    }
    return out;
}

/** The value of android:exported for a provider that does not write it:
 *  ParsedProviderUtils.parseProvider reads the attribute with
 *  getBoolean(exported, targetSdkVersion < JELLY_BEAN_MR1), where
 *  targetSdkVersion is the package's at the time `<application>` is parsed.
 *
 *  ParsingPackageUtils.parseBaseApkTags reads the children of `<manifest>`
 *  in order, and parseUsesSdk sets the target SDK.  So only a `<uses-sdk>`
 *  before `<application>` counts (aapt2's ManifestFixer inserts the one it
 *  adds before `<application>` for this reason), and the last one wins.
 *  Without one, the target SDK is ParsingUtils.DEFAULT_TARGET_SDK_VERSION,
 *  0.  In parseUsesSdk, a targetSdkVersion that peekValue() gives no value
 *  for (missing, or a null value) is the minSdkVersion, which is
 *  DEFAULT_MIN_SDK_VERSION, 1, when it gives no value either.  A codename
 *  (text) becomes the development version or fails the install; dexpp reads
 *  it as unknown.
 *
 *  nullopt when dexpp cannot tell, or when the answer is not the same in
 *  every configuration of a referenced value. */
std::optional<bool> provider_exported_default(const AttributeLookup &attrs,
                                              const axml::XmlNode &root)
{
    constexpr int64_t kJellyBeanMr1 = 17;
    const axml::XmlNode *uses_sdk = nullptr;
    for (const auto &child : root.children) {
        if (child.name == "application")
            break;
        if (child.name == "uses-sdk")
            uses_sdk = &child;
    }
    if (uses_sdk == nullptr)
        return true; // target SDK 0

    // Every reading of an attribute; a missing one reads as Null.
    auto readings = [](const AttributeRead<int64_t> &read) {
        return read.present ? read.all : std::vector{Reading<int64_t>::null()};
    };
    // Every target SDK the parser can end with.
    std::vector<Reading<int64_t>> targets;
    for (const Reading<int64_t> &target :
         readings(attrs.integer(*uses_sdk, framework::kTargetSdkVersion))) {
        if (target.kind != Reading<int64_t>::Kind::Null) {
            targets.push_back(target);
            continue;
        }
        for (const Reading<int64_t> &min_sdk :
             readings(attrs.integer(*uses_sdk, framework::kMinSdkVersion)))
            targets.push_back(min_sdk.kind == Reading<int64_t>::Kind::Null ? Reading<int64_t>::of(1)
                                                                           : min_sdk);
    }

    std::optional<bool> answer;
    for (const Reading<int64_t> &r : targets) {
        if (r.kind != Reading<int64_t>::Kind::Value)
            return std::nullopt;
        const bool exported = r.value < kJellyBeanMr1;
        if (answer.has_value() && *answer != exported)
            return std::nullopt;
        answer = exported;
    }
    return answer;
}

/** Read one component: activity, `<activity-alias>`, service, receiver or
 *  provider. */
Component read_component(const AttributeLookup &attrs, const axml::XmlNode &node,
                         Component::Kind kind, std::string_view package,
                         const Permission &app_permission, std::optional<bool> provider_default)
{
    const bool is_alias = node.name == "activity-alias";
    const bool is_provider = kind == Component::Kind::Provider;
    Component c;
    c.kind = kind;
    auto name = attrs.text(node, framework::kName);
    c.name = qualify(name.value().value_or(std::string()), package);
    c.name_reference = name.reference;
    c.name_unknown = name.unknown();
    if (is_alias) {
        // ParsedActivityUtils.parseActivityAlias:
        // getNonConfigurationString(targetActivity, NATIVE_CONFIG_VERSION),
        // qualified with ParsingUtils.buildClassName.
        c.target_activity = qualify(
            attrs.text(node, framework::kTargetActivity).value().value_or(std::string()), package);
    }
    for (const auto *filter : node.children_named("intent-filter"))
        c.intent_filters.push_back(parse_filter(attrs, *filter));

    // Activities, aliases, services and receivers: the parser reads
    // android:exported only when it has a value (TypedArray.hasValue), and
    // then with getBoolean(attr, false); without a value, the intent filters
    // decide.  An alias does not take its target's (ParsedActivityImpl.
    // makeAlias does not copy it).  Providers: getBoolean(attr, default),
    // where the default depends on the target SDK (see
    // provider_exported_default).
    c.exported_default =
        is_provider ? provider_default : std::optional<bool>(!c.intent_filters.empty());
    auto exported = attrs.boolean(node, framework::kExported,
                                  is_provider ? std::nullopt : std::optional<bool>(false));
    c.exported = exported.value();
    c.exported_reference = exported.reference;
    c.exported_unknown = exported.unknown();
    c.exported_varies = exported.varies(c.exported_default);

    // ParsedActivityUtils (activities and receivers) and ParsedServiceUtils:
    // getNonConfigurationString(permission, 0), and the application's
    // permission when that returns null.  An alias keeps only its own
    // ("An alias will override permissions to allow referencing an Activity
    // through its alias without needing the original permission").
    auto own = attrs.text(node, framework::kPermission);
    const Permission inherited = is_alias ? Permission{} : app_permission;
    const Permission permission = Permission::of(own, inherited);
    c.permission = permission.value;
    c.permission_unknown = permission.unknown;
    c.permission_reference = own.reference;
    c.permission_inherited = own.gives_nothing() && inherited.gives_something();

    if (is_provider) {
        // ParsedProviderUtils: readPermission, else permission, else the
        // application's; the same for writePermission.
        auto read = attrs.text(node, framework::kReadPermission);
        const Permission read_permission = Permission::of(read, permission);
        c.read_permission = read_permission.value;
        c.read_permission_unknown = read_permission.unknown;
        c.read_permission_reference = read.reference;
        auto write = attrs.text(node, framework::kWritePermission);
        const Permission write_permission = Permission::of(write, permission);
        c.write_permission = write_permission.value;
        c.write_permission_unknown = write_permission.unknown;
        c.write_permission_reference = write.reference;
        c.path_permissions = path_permissions(attrs, node);
    }
    return c;
}

void collect_components(const AttributeLookup &attrs, const axml::XmlNode &app,
                        std::string_view package, const Permission &app_permission,
                        std::optional<bool> provider_default, std::vector<Component> &out)
{
    // Activities and aliases first, in manifest order: Android keeps both in
    // the package's activity list (ParsingPackageUtils.parseBaseApplication).
    for (const auto &node : app.children)
        if (node.name == "activity" || node.name == "activity-alias")
            out.push_back(read_component(attrs, node, Component::Kind::Activity, package,
                                         app_permission, provider_default));

    struct Tag
    {
        const char *tag;
        Component::Kind kind;
    };
    constexpr Tag kinds[] = {
        {"service", Component::Kind::Service},
        {"receiver", Component::Kind::Receiver},
        {"provider", Component::Kind::Provider},
    };
    for (const auto &tag : kinds)
        for (const auto *node : app.children_named(tag.tag))
            out.push_back(
                read_component(attrs, *node, tag.kind, package, app_permission, provider_default));
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
    version_code_ = version_code.value();
    version_code_reference_ = version_code.reference;
    version_code_unknown_ = version_code.unknown();
    auto version_name = attrs.text(root, framework::kVersionName);
    version_name_ = version_name.value();
    version_name_reference_ = version_name.reference;
    version_name_unknown_ = version_name.unknown();

    if (const auto *uses_sdk = root.child("uses-sdk")) {
        auto min_sdk = attrs.integer(*uses_sdk, framework::kMinSdkVersion);
        min_sdk_ = min_sdk.value();
        min_sdk_reference_ = min_sdk.reference;
        min_sdk_unknown_ = min_sdk.unknown();
        auto target_sdk = attrs.integer(*uses_sdk, framework::kTargetSdkVersion);
        target_sdk_ = target_sdk.value();
        target_sdk_reference_ = target_sdk.reference;
        target_sdk_unknown_ = target_sdk.unknown();
    }

    for (const auto *up : root.children_named("uses-permission")) {
        std::string n = attrs.literal_text(*up, framework::kName).value_or(std::string());
        if (!n.empty())
            permissions_.push_back(std::move(n));
    }

    // signatureOrSystem is deprecated but still appears; both 2 and 3 mean the
    // permission requires the app's signature (or system), so a third-party
    // app cannot hold it. The base level is the low nibble; higher bits are flags.
    constexpr int kProtectionMaskBase = 0x0f;
    constexpr int kProtectionNormal = 0;
    constexpr int kProtectionSignature = 2;
    constexpr int kProtectionSignatureOrSystem = 3;
    auto is_signature = [&](const Reading<int64_t> &level) {
        if (level.kind == Reading<int64_t>::Kind::Unknown)
            return false;
        // ParsedPermissionUtils reads the level with getInt(attr,
        // PROTECTION_NORMAL): a null value is normal.
        const int64_t raw =
            level.kind == Reading<int64_t>::Kind::Value ? level.value : kProtectionNormal;
        const int base = static_cast<int>(raw) & kProtectionMaskBase;
        return base == kProtectionSignature || base == kProtectionSignatureOrSystem;
    };

    for (const auto *dp : root.children_named("permission")) {
        std::string n = attrs.text(*dp, framework::kName).value().value_or(std::string());
        if (n.empty())
            continue;
        // protectionLevel is normal (0) when omitted or null, and unknown
        // (nullopt) when it is a reference whose value cannot be told.
        auto level = attrs.integer(*dp, framework::kProtectionLevel);
        ProtectionLevel known;
        if (!level.unknown())
            known.value = static_cast<int>(level.value().value_or(kProtectionNormal));
        known.varies = level.varies(kProtectionNormal);
        // The guard counts only when every configuration gives a signature
        // level: on a device that matches a weaker one, a third-party app
        // can hold the permission.
        known.always_signature =
            level.present && std::all_of(level.all.begin(), level.all.end(), is_signature);
        declared_permission_levels_[n] = known;
        declared_permissions_.push_back(std::move(n));
    }

    if (const auto *app = root.child("application")) {
        // ParsingPackageUtils reads both with TypedArray.getBoolean:
        // debuggable defaults to false, allowBackup to true.
        auto debuggable = attrs.boolean(*app, framework::kDebuggable, false);
        debuggable_ = debuggable.value();
        debuggable_reference_ = debuggable.reference;
        debuggable_unknown_ = debuggable.unknown();
        debuggable_varies_ = debuggable.varies(false);
        auto allow_backup = attrs.boolean(*app, framework::kAllowBackup, true);
        allow_backup_ = allow_backup.value();
        allow_backup_reference_ = allow_backup.reference;
        allow_backup_unknown_ = allow_backup.unknown();
        allow_backup_varies_ = allow_backup.varies(true);
        // ParsingPackageUtils: getNonConfigurationString(permission, 0),
        // read before the components, which fall back to it.
        auto permission = attrs.text(*app, framework::kPermission);
        application_permission_ = permission.value();
        application_permission_reference_ = permission.reference;
        application_permission_unknown_ = permission.unknown();
        const Permission app_permission = Permission::of(permission, Permission{});
        collect_components(attrs, *app, package_, app_permission,
                           provider_exported_default(attrs, root), components_);
    }

    // Resolve each component's guard against the app-declared permission
    // levels.  A permission that is unknown, undeclared (e.g. a platform
    // permission), or below signature in some configuration does not guard.
    auto guards = [&](const std::optional<std::string> &permission) {
        if (!permission.has_value())
            return false;
        auto it = declared_permission_levels_.find(*permission);
        return it != declared_permission_levels_.end() && it->second.always_signature;
    };
    // A path permission that is absent gives no access; one that is unknown
    // might give any.
    auto path_guards = [&](const std::optional<std::string> &permission, bool unknown) {
        if (unknown)
            return false;
        return !permission.has_value() || guards(permission);
    };
    for (auto &c : components_) {
        if (c.kind != Component::Kind::Provider) {
            c.guarded_by_signature_permission = guards(c.permission);
            continue;
        }
        // A provider is reached by reading or by writing, and through the
        // permissions of its paths, which the activity manager and
        // ContentProvider accept in place of the provider's own: all of them
        // must be guarded.
        bool guarded = guards(c.read_permission) && guards(c.write_permission);
        for (const PathPermission &pp : c.path_permissions)
            guarded = guarded && path_guards(pp.read_permission, pp.read_permission_unknown) &&
                      path_guards(pp.write_permission, pp.write_permission_unknown);
        c.guarded_by_signature_permission = guarded;
    }
}

std::optional<int> Manifest::declared_permission_protection_level(std::string_view name) const
{
    auto it = declared_permission_levels_.find(std::string(name));
    if (it == declared_permission_levels_.end())
        return std::nullopt;
    return it->second.value; // nullopt when the level is unknown
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

std::optional<std::string_view> Manifest::application_permission() const
{
    if (application_permission_.has_value())
        return *application_permission_;
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
