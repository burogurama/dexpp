#pragma once

/** Typed view over a decoded AndroidManifest.xml.
 *
 *  This is the security-triage surface: package identity, SDK levels,
 *  permissions, and the four component kinds with their exported flags and
 *  intent filters.
 *
 *  Values are read the way Android's package parser reads them.  A boolean
 *  attribute written as text (android:exported="true" stored as a string in a
 *  hand-made binary manifest, or a reference to a `<string>` resource) is
 *  true only for exactly "1", "true" or "TRUE", as TypedArray.getBoolean
 *  reads it; any other text is false, and empty text gives the attribute's
 *  default.
 *
 *  An attribute can be written as a resource reference, such as
 *  android:exported="@bool/is_exported" or
 *  android:permission="@string/guard".  When the manifest is built with the
 *  APK's resource table (as apk::Apk::manifest() does), such a value is
 *  resolved through it.  The reference id itself is kept next to the value
 *  (Component::exported_reference, Manifest::debuggable_reference(), ...).
 *  Each attribute with a reference member also has an `_unknown` member
 *  (Component::exported_unknown, Manifest::debuggable_unknown(), ...).  It
 *  is true when dexpp cannot tell what Android reads: the reference cannot
 *  be resolved (no resource table, an id the table does not have, a theme
 *  attribute "?attr/...", a value of a type the attribute does not take), or
 *  a text value depends on the device.  The value is then nullopt.
 *
 *  A null value counts as not written, as on Android: the attribute written
 *  as `@null` or `@empty`, or a reference whose resource is `@null` or
 *  `@empty` (directly or through other references).  TypedArray.hasValue()
 *  is false for it, and the other TypedArray getters give their default.  So
 *  the value is nullopt, as for a missing attribute, and the `_unknown`
 *  member is false; for a reference, the reference id is kept.  `@null`
 *  written directly is a reference to id 0, which is no resource: it reads
 *  exactly like a missing attribute, without a reference id.
 *
 *  So for an attribute with a reference id:
 *  - value set: the value Android reads;
 *  - value nullopt, `_unknown` false: Android reads it as not written;
 *  - value nullopt, `_unknown` true: dexpp cannot tell.
 *
 *  A resource can have a different value in each configuration (language,
 *  screen, Android version...), and a device uses the one that matches it.
 *  How each kind of attribute deals with that:
 *
 *  - Booleans (exported, debuggable, allowBackup) and the protection level of
 *    a declared permission hold the default configuration's value.  A second
 *    member (Component::exported_varies, Manifest::debuggable_varies(), ...)
 *    is true when Android reads another value in other configurations; a
 *    null value there counts as what Android does when the attribute is not
 *    written (the attribute's default, or for exported the intent-filter
 *    rule).  The security answers (Component::is_exported(), is_reachable())
 *    take the unsafe value when one exists: exported if any configuration
 *    says so, and not guarded if any configuration gives a level below
 *    signature.
 *  - Integers (versionCode, minSdkVersion, targetSdkVersion) hold the default
 *    configuration's value.
 *  - Text (android:name, android:permission, android:readPermission,
 *    android:writePermission, android:versionName) is read with
 *    TypedArray.getNonConfigurationString, which returns null for a string
 *    resource that changes with the locale, the screen or any other
 *    configuration except the Android version.  So such a reference reads
 *    as not written (nullopt, reference set, `_unknown` false), as Android
 *    ignores it.  A string that differs only between Android versions is
 *    device dependent: nullopt, with `_unknown` true.  The names of
 *    `<uses-permission>`, `<action>` and `<category>` are never resolved, as
 *    Android does not resolve them.
 *
 *  The permission of a component is the one Android enforces: its own
 *  android:permission, or the application's when it gives none (see
 *  Component::permission). */

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
    /** android:name, normalized to a fully-qualified class.  Empty when the
     *  attribute is missing, or when it is a reference that gives no value
     *  (then name_reference is set). */
    std::string name;
    /** The resource id when android:name is written as a reference, such as
     *  "@string/main_activity", whether it was resolved or not; nullopt for
     *  a plain value or no attribute. */
    std::optional<uint32_t> name_reference;
    /** True when android:name is a reference whose string dexpp cannot tell
     *  (see the comment at the top of this file). */
    bool name_unknown = false;
    /** android:exported: the value written, or the value of the resource it
     *  refers to, in the default configuration.  nullopt when the attribute
     *  is absent, when it is a null value (Android reads it as not written),
     *  or when its value is unknown (exported_unknown). */
    std::optional<bool> exported;
    /** The resource id when android:exported is written as a reference, such
     *  as "@bool/is_exported" (or a theme attribute, "?attr/name"), whether it
     *  was resolved or not; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> exported_reference;
    /** True when android:exported is a reference whose value in the default
     *  configuration dexpp cannot tell.  `exported` is then nullopt. */
    bool exported_unknown = false;
    /** True when android:exported is a reference that makes the component
     *  exported in some configurations and not in others: for example false
     *  in res/values and true in res/values-v31, so the component is exported
     *  on Android 12 and newer.  A configuration whose value is null uses the
     *  intent-filter rule, and one whose value is unknown counts as another
     *  answer.  `exported` holds the default configuration's value. */
    bool exported_varies = false;
    /** The permission a caller needs, as Android enforces it
     *  (ActivityInfo.permission, ServiceInfo.permission): the component's
     *  android:permission, or, when the component gives none, the
     *  application's android:permission (then permission_inherited is true).
     *  "Gives none" means the attribute is missing, or Android reads it as
     *  not written: a null value, or a string that changes with the
     *  configuration (see the comment at the top of this file).  nullopt
     *  when neither gives one, or when the permission is unknown
     *  (permission_unknown).
     *
     *  A provider has a read and a write permission instead (read_permission,
     *  write_permission); for a provider this is the permission they fall
     *  back to. */
    std::optional<std::string> permission;
    /** The resource id when the component's own android:permission is
     *  written as a reference, such as "@string/guard", whether it was
     *  resolved or not; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> permission_reference;
    /** True when dexpp cannot tell `permission`: the component's own
     *  android:permission is a reference dexpp cannot read, or the component
     *  gives none and the application's is unknown. */
    bool permission_unknown = false;
    /** True when `permission` (or permission_unknown) comes from the
     *  `<application>` element: the component gives no permission of its
     *  own, and the application's android:permission gives one or is
     *  unknown. */
    bool permission_inherited = false;
    /** For a provider: the permission a caller needs to read from it
     *  (ProviderInfo.readPermission).  It is the provider's
     *  android:readPermission, or `permission` when that gives none (so it
     *  can come from the application too).  nullopt for the other kinds,
     *  when no permission applies, or when it is unknown
     *  (read_permission_unknown). */
    std::optional<std::string> read_permission;
    /** The resource id when the provider's android:readPermission is written
     *  as a reference; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> read_permission_reference;
    /** True when dexpp cannot tell read_permission. */
    bool read_permission_unknown = false;
    /** For a provider: the permission a caller needs to write to it
     *  (ProviderInfo.writePermission): android:writePermission, or
     *  `permission` when that gives none.  Same rules as read_permission. */
    std::optional<std::string> write_permission;
    /** The resource id when the provider's android:writePermission is
     *  written as a reference; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> write_permission_reference;
    /** True when dexpp cannot tell write_permission. */
    bool write_permission_unknown = false;
    std::vector<IntentFilter> intent_filters;

    /** Resolved at parse time: true when the permission Android enforces
     *  names a permission the app itself declares with a signature /
     *  signatureOrSystem protection level, one a third-party app cannot
     *  obtain.  For an activity, a service or a receiver that is
     *  `permission`; for a provider, both read_permission and
     *  write_permission must be such a permission, as an app that can read
     *  or write reaches it.  Permissions whose protection level the manifest
     *  does not declare (platform permissions like android.permission.DUMP)
     *  leave this false, and so do an unknown permission and a level given
     *  by a reference that is below signature in some configuration, or
     *  cannot be resolved. */
    bool guarded_by_signature_permission = false;

    /** Effective exported state: the explicit flag if present, else inferred —
     *  a component with at least one intent filter defaults to exported.  A
     *  null value counts as not written, so it is inferred too.
     *
     *  When android:exported is a reference whose value differs between
     *  configurations (exported_varies), this is true: the component is
     *  exported on the devices that match a configuration that says so.
     *
     *  When android:exported is a reference whose value is unknown
     *  (exported_unknown), the developer did set the flag but its value
     *  cannot be told, and the intent-filter rule would be a guess that can
     *  hide an exported component.  For triage the safe answer is "possibly
     *  exported", so this returns true. */
    bool is_exported() const
    {
        if (exported_varies)
            return true;
        if (exported.has_value())
            return *exported;
        if (exported_unknown)
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
    /** Read @p doc.  With @p resources (the APK's resource table),
     *  attributes given as resource references are resolved through it;
     *  without, they keep only their reference id.  The table is only read
     *  here, not kept. */
    explicit Manifest(axml::XmlDocument doc, const ResourceTable *resources = nullptr);

    /** package attribute of the `<manifest>` element. */
    std::string_view package() const { return package_; }
    /** android:versionCode, if present and known. */
    std::optional<int64_t> version_code() const { return version_code_; }
    /** android:versionName, if present as text or as a reference to a
     *  string that is the same in every configuration. */
    std::optional<std::string_view> version_name() const;
    /** uses-sdk android:minSdkVersion / targetSdkVersion, if present and
     *  known. */
    std::optional<int64_t> min_sdk() const { return min_sdk_; }
    std::optional<int64_t> target_sdk() const { return target_sdk_; }

    /** The resource id when android:versionCode, minSdkVersion,
     *  targetSdkVersion, debuggable or allowBackup is written as a reference
     *  (see the comment at the top of this file); nullopt for a plain value
     *  or no attribute.  When the matching value accessor returns nullopt and
     *  this is set, the matching `_unknown` accessor says whether the value
     *  is unknown, or Android reads it as not written (a null value). */
    std::optional<uint32_t> version_code_reference() const { return version_code_reference_; }
    std::optional<uint32_t> min_sdk_reference() const { return min_sdk_reference_; }
    std::optional<uint32_t> target_sdk_reference() const { return target_sdk_reference_; }
    std::optional<uint32_t> debuggable_reference() const { return debuggable_reference_; }
    std::optional<uint32_t> allow_backup_reference() const { return allow_backup_reference_; }
    /** The resource id when android:versionName is written as a reference;
     *  nullopt for a plain value or no attribute. */
    std::optional<uint32_t> version_name_reference() const { return version_name_reference_; }

    /** True when the attribute is a reference whose value (in the default
     *  configuration) dexpp cannot tell; the value accessor then returns
     *  nullopt.  For versionName, true also when the string differs between
     *  Android versions, so its value depends on the device. */
    bool version_code_unknown() const { return version_code_unknown_; }
    bool version_name_unknown() const { return version_name_unknown_; }
    bool min_sdk_unknown() const { return min_sdk_unknown_; }
    bool target_sdk_unknown() const { return target_sdk_unknown_; }
    bool debuggable_unknown() const { return debuggable_unknown_; }
    bool allow_backup_unknown() const { return allow_backup_unknown_; }

    /** True when android:debuggable or android:allowBackup is a reference
     *  whose value is not the same in every configuration: for example
     *  false in res/values and true in res/values-v31.  A configuration whose
     *  value is null counts as the attribute's default (false for
     *  debuggable, true for allowBackup), as Android reads it.  debuggable()
     *  and allow_backup() hold the default configuration's value; on a device
     *  that matches another configuration, Android uses that one.  For
     *  triage, treat a varying value as possibly true. */
    bool debuggable_varies() const { return debuggable_varies_; }
    bool allow_backup_varies() const { return allow_backup_varies_; }

    /** android:permission of the `<application>` element: the permission
     *  every activity, service, receiver and provider requires when it does
     *  not give one of its own (see Component::permission).  nullopt when the
     *  attribute is missing, when Android reads it as not written (a null
     *  value, or a string that changes with the configuration), or when it
     *  is unknown (application_permission_unknown()). */
    std::optional<std::string_view> application_permission() const;
    /** The resource id when the application's android:permission is written
     *  as a reference; nullopt for a plain value or no attribute. */
    std::optional<uint32_t> application_permission_reference() const
    {
        return application_permission_reference_;
    }
    /** True when the application's android:permission is a reference whose
     *  string dexpp cannot tell. */
    bool application_permission_unknown() const { return application_permission_unknown_; }

    /** android:name of every `<uses-permission>`. */
    const std::vector<std::string> &permissions() const { return permissions_; }
    /** android:name of every `<permission>` the app itself declares. */
    const std::vector<std::string> &declared_permissions() const { return declared_permissions_; }
    /** android:protectionLevel of an app-declared `<permission>` (the raw Android
     *  value), or nullopt if the permission is not declared by this app, or
     *  its level is a resource reference whose value is unknown.  A
     *  declaration without the attribute, or with a null value, is normal
     *  (0), as Android reads it.  The base level is the low nibble: 0
     *  normal, 1 dangerous, 2 signature, 3 signatureOrSystem; higher bits are
     *  flags (e.g. 0x10 privileged).  A permission whose level is unknown
     *  does not make a component guarded_by_signature_permission.  For a
     *  level given by a reference, this is the default configuration's
     *  value. */
    std::optional<int> declared_permission_protection_level(std::string_view name) const;

    /** True when the app declares permission @p name with a protection level
     *  given by a reference whose value is not the same in every
     *  configuration.  Such a permission guards a component only when every
     *  configuration gives signature or signatureOrSystem: the weakest level
     *  is the one that counts.  False for a permission the app does not
     *  declare. */
    bool declared_permission_protection_level_varies(std::string_view name) const;

    /** All components, or those of one kind. */
    const std::vector<Component> &components() const { return components_; }
    std::vector<const Component *> components_of(Component::Kind kind) const;

    /** The launcher activity's name, if any (MAIN + LAUNCHER intent filter). */
    std::optional<std::string_view> launcher_activity() const;

    /** application android:debuggable, if explicitly set and known.  nullopt
     *  also for a null value, which Android reads as not written (false). */
    std::optional<bool> debuggable() const { return debuggable_; }
    /** application android:allowBackup, if explicitly set and known.  nullopt
     *  also for a null value, which Android reads as not written (true). */
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
    std::optional<std::string> application_permission_;
    std::optional<uint32_t> version_code_reference_;
    std::optional<uint32_t> min_sdk_reference_;
    std::optional<uint32_t> target_sdk_reference_;
    std::optional<uint32_t> debuggable_reference_;
    std::optional<uint32_t> allow_backup_reference_;
    std::optional<uint32_t> version_name_reference_;
    std::optional<uint32_t> application_permission_reference_;
    bool version_code_unknown_ = false;
    bool version_name_unknown_ = false;
    bool min_sdk_unknown_ = false;
    bool target_sdk_unknown_ = false;
    bool debuggable_unknown_ = false;
    bool allow_backup_unknown_ = false;
    bool application_permission_unknown_ = false;
    bool debuggable_varies_ = false;
    bool allow_backup_varies_ = false;
    std::vector<std::string> permissions_;
    std::vector<std::string> declared_permissions_;
    /** The protection level of a declared permission. */
    struct ProtectionLevel
    {
        /** The default configuration's value; nullopt when it is
         *  unknown. */
        std::optional<int> value;
        /** The reference gives other values in other configurations. */
        bool varies = false;
        /** Every configuration gives signature or signatureOrSystem. */
        bool always_signature = false;
    };
    std::unordered_map<std::string, ProtectionLevel> declared_permission_levels_;
    std::vector<Component> components_;
};

} // namespace dex::apk
