#pragma once

/** Typed view over a decoded AndroidManifest.xml.
 *
 *  This is the security-triage surface: package identity, SDK levels,
 *  permissions, and the four component kinds with their exported flags and
 *  intent filters.  Attribute values that are resource references (e.g.
 *  android:label="@string/app_name") are NOT resolved here — that needs the
 *  resource table; such accessors return the raw reference id. */

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "axml.hpp"

namespace dex::apk {

/** An <intent-filter> distilled to the parts that matter for triage. */
struct IntentFilter
{
    std::vector<std::string> actions;    ///< android:name of each <action>.
    std::vector<std::string> categories; ///< android:name of each <category>.

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
    std::string name;                      ///< android:name, normalized to a fully-qualified class.
    std::optional<bool> exported;          ///< android:exported, if explicitly set.
    std::optional<std::string> permission; ///< android:permission guarding the component.
    std::vector<IntentFilter> intent_filters;

    /** Resolved at parse time: true when `permission` names a permission the app
     *  itself declares with a signature / signatureOrSystem protection level —
     *  i.e. one a third-party app cannot obtain. Permissions whose protection
     *  level the manifest does not declare (platform permissions like
     *  android.permission.DUMP) leave this false. */
    bool guarded_by_signature_permission = false;

    /** Effective exported state: the explicit flag if present, else inferred —
     *  a component with at least one intent filter defaults to exported. */
    bool is_exported() const { return exported.value_or(!intent_filters.empty()); }

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
    explicit Manifest(axml::XmlDocument doc);

    /** package attribute of the <manifest> element. */
    std::string_view package() const { return package_; }
    /** android:versionCode, if present. */
    std::optional<int64_t> version_code() const { return version_code_; }
    /** android:versionName, if present (may be a raw string or a ref id as text). */
    std::optional<std::string_view> version_name() const;
    /** uses-sdk android:minSdkVersion / targetSdkVersion, if present. */
    std::optional<int64_t> min_sdk() const { return min_sdk_; }
    std::optional<int64_t> target_sdk() const { return target_sdk_; }

    /** android:name of every <uses-permission>. */
    const std::vector<std::string> &permissions() const { return permissions_; }
    /** android:name of every <permission> the app itself declares. */
    const std::vector<std::string> &declared_permissions() const { return declared_permissions_; }
    /** android:protectionLevel of an app-declared <permission> (the raw Android
     *  value), or nullopt if the permission is not declared by this app. The
     *  base level is the low nibble: 0 normal, 1 dangerous, 2 signature,
     *  3 signatureOrSystem; higher bits are flags (e.g. 0x10 privileged). */
    std::optional<int> declared_permission_protection_level(std::string_view name) const;

    /** All components, or those of one kind. */
    const std::vector<Component> &components() const { return components_; }
    std::vector<const Component *> components_of(Component::Kind kind) const;

    /** The launcher activity's name, if any (MAIN + LAUNCHER intent filter). */
    std::optional<std::string_view> launcher_activity() const;

    /** application android:debuggable, if explicitly set. */
    std::optional<bool> debuggable() const { return debuggable_; }
    /** application android:allowBackup, if explicitly set. */
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
    std::vector<std::string> permissions_;
    std::vector<std::string> declared_permissions_;
    std::unordered_map<std::string, int> declared_permission_levels_;
    std::vector<Component> components_;
};

} // namespace dex::apk
