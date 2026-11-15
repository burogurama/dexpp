#pragma once

/** Typed view over a decoded AndroidManifest.xml.
 *
 *  This is the security-triage surface: package identity, SDK levels,
 *  permissions, and the four component kinds with their exported flags and
 *  intent filters.
 *
 *  A boolean or integer attribute can be written as a resource reference,
 *  such as android:exported="@bool/is_exported".  When the manifest is built
 *  with the APK's resource table (as apk::Apk::manifest() does), such a value
 *  is resolved through it, using the default configuration.  The reference id
 *  itself is kept next to the value (Component::exported_reference,
 *  Manifest::debuggable_reference(), ...).  A reference that cannot be
 *  resolved — no resource table, an id the table does not have, a value of
 *  another type, or a theme attribute ("?attr/...") — leaves the value
 *  nullopt but keeps the reference id, so "not written" (both nullopt) and
 *  "written, value unknown" (value nullopt, reference set) stay apart.
 *  Text attributes (android:name, android:permission, android:versionName)
 *  are read as written; a reference there is not resolved. */

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "axml.hpp"
#include "resources.hpp"

namespace dex::apk {

/** An `<intent-filter>` distilled to the parts that matter for triage. */
struct IntentFilter
{
    std::vector<std::string> actions;    ///< android:name of each `<action>`.
    std::vector<std::string> categories; ///< android:name of each `<category>`.

    bool has_action(std::string_view name) const
    {
        for (const auto &a : actions)
            if (a == name)
                return true;
        return false;
    }
    /** True for the launcher entry point (MAIN + LAUNCHER). */
    bool is_launcher() const
    {
        bool main = has_action("android.intent.action.MAIN");
        bool launcher = false;
        for (const auto &c : categories)
            if (c == "android.intent.category.LAUNCHER")
                launcher = true;
        return main && launcher;
    }
};

/** One of activity / service / receiver / provider. */
struct Component
{
    enum class Kind : uint8_t { Activity, Service, Receiver, Provider };

    Kind kind;
    std::string name; ///< android:name, normalized to a fully-qualified class.
    /** android:exported: the value written, or the value of the resource it
     *  refers to.  nullopt when the attribute is absent, or when it is a
     *  reference that could not be resolved (then exported_reference is
     *  set). */
    std::optional<bool> exported;
    /** The resource id when android:exported is written as a reference, such
     *  as "@bool/is_exported" (or a theme attribute, "?attr/name"), whether it
     *  was resolved or not; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> exported_reference;
    std::optional<std::string> permission; ///< android:permission guarding the component.
    std::vector<IntentFilter> intent_filters;

    /** Resolved at parse time: true when `permission` names a permission the app
     *  itself declares with a signature / signatureOrSystem protection level —
     *  i.e. one a third-party app cannot obtain. Permissions whose protection
     *  level the manifest does not declare (platform permissions like
     *  android.permission.DUMP) leave this false. */
    bool guarded_by_signature_permission = false;

    /** Effective exported state: the explicit flag if present, else inferred —
     *  a component with at least one intent filter defaults to exported.
     *
     *  When android:exported is a reference that could not be resolved, the
     *  developer did set the flag but its value is unknown, and the
     *  intent-filter rule would be a guess that can hide an exported
     *  component.  For triage the safe answer is "possibly exported", so this
     *  returns true. */
    bool is_exported() const
    {
        if (exported.has_value())
            return *exported;
        if (exported_reference.has_value())
            return true;
        return !intent_filters.empty();
    }

    /** Reachable by a third-party app: exported and not gated behind a
     *  signature-level permission the app declares. Normal/dangerous-protected
     *  components stay reachable (a malicious app can hold those), as do
     *  components guarded by an undeclared (e.g. platform) permission. */
    bool is_reachable() const { return is_exported() && !guarded_by_signature_permission; }
};

/** Typed AndroidManifest.xml.  Built from an axml::XmlDocument. */
class Manifest
{
  public:
    /** Read @p doc.  With @p resources (the APK's resource table), boolean and
     *  integer attributes given as resource references are resolved through
     *  it; without, they keep only their reference id.  The table is only
     *  read here, not kept. */
    explicit Manifest(axml::XmlDocument doc, const ResourceTable *resources = nullptr);

    /** package attribute of the `<manifest>` element. */
    std::string_view package() const { return package_; }
    /** android:versionCode, if present and known. */
    std::optional<int64_t> version_code() const { return version_code_; }
    /** android:versionName, if present as text. */
    std::optional<std::string_view> version_name() const;
    /** uses-sdk android:minSdkVersion / targetSdkVersion, if present and
     *  known. */
    std::optional<int64_t> min_sdk() const { return min_sdk_; }
    std::optional<int64_t> target_sdk() const { return target_sdk_; }

    /** The resource id when android:versionCode, minSdkVersion,
     *  targetSdkVersion, debuggable or allowBackup is written as a reference
     *  (see the comment at the top of this file); nullopt for a plain value
     *  or no attribute.  When the matching value accessor returns nullopt and
     *  this is set, the reference could not be resolved and the value is
     *  unknown. */
    std::optional<uint32_t> version_code_reference() const { return version_code_reference_; }
    std::optional<uint32_t> min_sdk_reference() const { return min_sdk_reference_; }
    std::optional<uint32_t> target_sdk_reference() const { return target_sdk_reference_; }
    std::optional<uint32_t> debuggable_reference() const { return debuggable_reference_; }
    std::optional<uint32_t> allow_backup_reference() const { return allow_backup_reference_; }

    /** android:name of every `<uses-permission>`. */
    const std::vector<std::string> &permissions() const { return permissions_; }
    /** android:name of every `<permission>` the app itself declares. */
    const std::vector<std::string> &declared_permissions() const { return declared_permissions_; }
    /** android:protectionLevel of an app-declared `<permission>` (the raw Android
     *  value), or nullopt if the permission is not declared by this app, or
     *  its level is a resource reference that could not be resolved.  A
     *  declaration without the attribute is normal (0).  The base level is
     *  the low nibble: 0 normal, 1 dangerous, 2 signature, 3
     *  signatureOrSystem; higher bits are flags (e.g. 0x10 privileged).  A
     *  permission whose level is unknown does not make a component
     *  guarded_by_signature_permission. */
    std::optional<int> declared_permission_protection_level(std::string_view name) const;

    /** All components, or those of one kind. */
    const std::vector<Component> &components() const { return components_; }
    std::vector<const Component *> components_of(Component::Kind kind) const;

    /** The launcher activity's name, if any (MAIN + LAUNCHER intent filter). */
    std::optional<std::string_view> launcher_activity() const;

    /** application android:debuggable, if explicitly set and known. */
    std::optional<bool> debuggable() const { return debuggable_; }
    /** application android:allowBackup, if explicitly set and known. */
    std::optional<bool> allow_backup() const { return allow_backup_; }

    /** The underlying decoded XML, for attributes this view does not surface. */
    const axml::XmlDocument &xml() const { return doc_; }

  private:
    axml::XmlDocument doc_;
    std::string package_;
    std::optional<int64_t> version_code_;
    std::optional<std::string> version_name_;
    std::optional<int64_t> min_sdk_;
    std::optional<int64_t> target_sdk_;
    std::optional<bool> debuggable_;
    std::optional<bool> allow_backup_;
    std::optional<uint32_t> version_code_reference_;
    std::optional<uint32_t> min_sdk_reference_;
    std::optional<uint32_t> target_sdk_reference_;
    std::optional<uint32_t> debuggable_reference_;
    std::optional<uint32_t> allow_backup_reference_;
    std::vector<std::string> permissions_;
    std::vector<std::string> declared_permissions_;
    /** Protection level of each declared permission; nullopt when it is an
     *  unresolved reference. */
    std::unordered_map<std::string, std::optional<int>> declared_permission_levels_;
    std::vector<Component> components_;
};

} // namespace dex::apk
