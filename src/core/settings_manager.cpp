#include "Common.h"
#include "settings_manager.h"
#include "event_system.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace x4n {

SettingsManager::Map SettingsManager::s_map;

// ---------------------------------------------------------------------------
// Registration / lifecycle
// ---------------------------------------------------------------------------

void SettingsManager::register_extension(std::string const &extension_id, std::vector<SettingSchema> schema)
{
    if (schema.empty())
        return;
    ExtensionSettings &ext = s_map[extension_id];
    ext.schema = std::move(schema);
    ext.values.clear();
    ext.values_loaded = false;
    // Hot-reload may change schema shape (added/removed settings, renamed
    // options). Clear the ABI cache vectors so rebuild_abi_cache sees a
    // clean slate — not just the "dirty" flag.
    ext.abi_cache.clear();
    ext.options_cache.clear();
    ext.abi_cache_built = false;
    Logger::info("Settings: registered {} setting(s) for extension '{}'",
                 ext.schema.size(), extension_id);
}

void SettingsManager::unregister_extension(std::string const &extension_id)
{
    s_map.erase(extension_id);
}

void SettingsManager::shutdown()
{
    s_map.clear();
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

ExtensionSettings *SettingsManager::find(std::string const &extension_id)
{
    auto it = s_map.find(extension_id);
    return it == s_map.end() ? nullptr : &it->second;
}

SettingSchema const *SettingsManager::schema_for(ExtensionSettings const &ext, std::string const &key)
{
    for (auto const &s : ext.schema)
        if (s.id == key)
            return &s;
    return nullptr;
}

bool SettingsManager::has_key(std::string const &extension_id, std::string const &key)
{
    auto *ext = find(extension_id);
    return ext && schema_for(*ext, key) != nullptr;
}

SettingType SettingsManager::type_of(std::string const &extension_id, std::string const &key)
{
    auto *ext = find(extension_id);
    if (!ext)
        return SettingType::Toggle;
    auto *s = schema_for(*ext, key);
    return s ? s->type : SettingType::Toggle;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

fs::path SettingsManager::user_file_path(std::string const &extension_id)
{
    // Reuse the same path logic the logger uses so framework + settings land
    // together under <profile>\x4native\<extension_id>\.
    fs::path ext_dir = Logger::profile_ext_dir(extension_id);
    if (ext_dir.empty()) {
        Logger::warn("Settings: profile path unresolved; cannot persist '{}'", extension_id);
        return {};
    }
    return ext_dir / "settings.user.json";
}

void SettingsManager::ensure_loaded(std::string const &extension_id, ExtensionSettings &ext)
{
    if (ext.values_loaded)
        return;

    // Seed any key that isn't already in the value map with its default. We
    // only fill gaps (instead of rebuilding from scratch) because an earlier
    // call may have returned without committing `values_loaded=true` (profile
    // dir not yet resolvable) while extension code set values in the
    // meantime — we don't want to clobber those on retry.
    for (SettingSchema const &s : ext.schema) {
        if (ext.values.contains(s.id))
            continue;
        switch (s.type) {
        case SettingType::Toggle:   ext.values[s.id] = s.default_bool;   break;
        case SettingType::Slider:   ext.values[s.id] = s.default_number; break;
        case SettingType::Dropdown: ext.values[s.id] = s.default_string; break;
        }
    }

    fs::path path = user_file_path(extension_id);
    if (path.empty()) {
        // Profile directory unresolvable (startup race before GameAPI is
        // ready). Defaults are seeded above so callers see sane values;
        // leave `values_loaded` false so the next call retries and picks
        // up user.json once the profile is online.
        return;
    }
    // Path resolves → this is a terminal attempt. Whether the file exists,
    // is missing, or is malformed, defaults apply and we don't retry.
    ext.values_loaded = true;

    std::ifstream f(path);
    if (!f.is_open())
        return;

    json doc;
    try {
        f >> doc;
    } catch (json::parse_error const &e) {
        Logger::warn("Settings: parse error in '{}': {}", path.string(), e.what());
        return;
    }

    if (!doc.is_object())
        return;

    unsigned loaded = 0;
    for (SettingSchema const &s : ext.schema) {
        auto it = doc.find(s.id);
        if (it == doc.end())
            continue;
        try {
            switch (s.type) {
            case SettingType::Toggle:
                if (it->is_boolean()) {
                    ext.values[s.id] = it->get<bool>();
                    ++loaded;
                }
                break;
            case SettingType::Slider:
                if (it->is_number()) {
                    ext.values[s.id] = it->get<double>();
                    ++loaded;
                }
                break;
            case SettingType::Dropdown:
                if (it->is_string()) {
                    auto v  = it->get<std::string>();
                    bool ok = std::ranges::any_of(
                        s.options, [&v](SettingOption const &o) { return o.id == v; }
                    );
                    if (ok) {
                        ext.values[s.id] = std::move(v);
                        ++loaded;
                    } else {
                        Logger::warn("Settings: '{}/{}' stored value '{}' not in current options — using default '{}'",
                                     extension_id, s.id, v, s.default_string);
                    }
                }
                break;
            }
        } catch (...) {
        }
    }

    Logger::info("Settings: loaded {} value(s) for '{}' from {}",
                 loaded, extension_id, path.string());
}

void SettingsManager::write_user_file(std::string const &extension_id, ExtensionSettings const &ext)
{
    fs::path path = user_file_path(extension_id);
    if (path.empty())
        return;

    json doc = json::object();
    for (SettingSchema const &s : ext.schema) {
        auto it = ext.values.find(s.id);
        if (it == ext.values.end())
            continue;
        std::visit([&doc, id = s.id](auto const &v) { doc[id] = v; },
                   it->second);
    }

    // Atomic write: temp file + rename.
    fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            Logger::warn("Settings: cannot open '{}' for write", tmp.string());
            return;
        }
        f << doc.dump(2);
    }

    // std::filesystem::rename atomically replaces the destination on both
    // Windows (MoveFileEx with MOVEFILE_REPLACE_EXISTING) and POSIX. No
    // remove+rename retry is needed.
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        Logger::warn("Settings: rename '{}' -> '{}' failed: {}", tmp.string(), path.string(), ec.message());
        std::error_code cleanup_ec;
        fs::remove(tmp, cleanup_ec); // best-effort: don't leave a stale .tmp behind
    }
}

// ---------------------------------------------------------------------------
// Typed getters / setters
// ---------------------------------------------------------------------------

bool SettingsManager::get_bool(
    std::string const &extension_id,
    std::string const &key, bool fallback)
{
    auto *ext = find(extension_id);
    if (!ext)
        return fallback;
    ensure_loaded(extension_id, *ext);
    auto it = ext->values.find(key);
    if (it == ext->values.end())
        return fallback;
    if (auto *p = std::get_if<bool>(&it->second))
        return *p;
    return fallback;
}

double SettingsManager::get_number(
    std::string const &extension_id,
    std::string const &key, double fallback)
{
    auto *ext = find(extension_id);
    if (!ext)
        return fallback;
    ensure_loaded(extension_id, *ext);
    auto it = ext->values.find(key);
    if (it == ext->values.end())
        return fallback;
    if (auto *p = std::get_if<double>(&it->second))
        return *p;
    return fallback;
}

char const *SettingsManager::get_string(
    std::string const &extension_id,
    std::string const &key, char const *fallback)
{
    auto *ext = find(extension_id);
    if (!ext)
        return fallback;
    ensure_loaded(extension_id, *ext);
    auto it = ext->values.find(key);
    if (it == ext->values.end())
        return fallback;
    if (auto *p = std::get_if<std::string>(&it->second))
        return p->c_str();
    return fallback;
}

void SettingsManager::fire_change(
    std::string const &extension_id,
    std::string const &key, SettingValue const &new_value)
{
    ::X4NativeSettingChanged payload = {
        .extension_id = extension_id.c_str(),
        .key          = key.c_str(),
    };
    std::visit(
        [&payload]<typename Ty>(Ty const &v) {
            using BaseT = std::decay_t<Ty>;
            if constexpr (std::is_same_v<BaseT, bool>) {
                payload.type = X4N_SETTING_TOGGLE;
                payload.b    = v ? 1 : 0;
            } else if constexpr (std::is_same_v<BaseT, double>) {
                payload.type = X4N_SETTING_SLIDER;
                payload.d    = v;
            } else if constexpr (std::is_same_v<BaseT, std::string>) {
                payload.type = X4N_SETTING_DROPDOWN;
                payload.s    = v.c_str();
            }
        },
        new_value
    );

    // Fire a scoped event for the owning extension so subscribers only
    // receive their own keys. Also fire the unscoped "on_setting_changed"
    // so cross-extension observers (debug tools, settings inspectors) can
    // tap into the firehose if needed.
    std::string scoped = "on_setting_changed:" + extension_id;
    EventSystem::fire(scoped.c_str(), &payload);
    EventSystem::fire("on_setting_changed", &payload);
}

void SettingsManager::set_bool(
    std::string const &extension_id,
    std::string const &key, bool value)
{
    auto *ext = find(extension_id);
    if (!ext)
        return;
    auto *s = schema_for(*ext, key);
    if (!s || s->type != SettingType::Toggle) {
        Logger::warn("Settings: set_bool on '{}/{}' rejected (schema mismatch)", extension_id, key);
        return;
    }

    ensure_loaded(extension_id, *ext);
    auto it = ext->values.find(key);
    if (it != ext->values.end()) {
        if (auto *p = std::get_if<bool>(&it->second); p && *p == value)
            return;
    }
    ext->values[key] = value;

    write_user_file(extension_id, *ext);
    refresh_abi_current_values(*ext);
    fire_change(extension_id, key, ext->values[key]);
}

void SettingsManager::set_number(
    std::string const &extension_id,
    std::string const &key, double value)
{
    auto *ext = find(extension_id);
    if (!ext)
        return;
    auto *s = schema_for(*ext, key);
    if (!s || s->type != SettingType::Slider) {
        Logger::warn("Settings: set_number on '{}/{}' rejected (schema mismatch)", extension_id, key);
        return;
    }
    // Clamp to schema bounds.
    value = std::clamp(value, s->min, s->max);

    ensure_loaded(extension_id, *ext);
    auto it = ext->values.find(key);
    if (it != ext->values.end())
        if (auto *p = std::get_if<double>(&it->second); p && *p == value)
            return;
    ext->values[key] = value;

    write_user_file(extension_id, *ext);
    refresh_abi_current_values(*ext);
    fire_change(extension_id, key, ext->values[key]);
}

void SettingsManager::set_string(
    std::string const &extension_id,
    std::string const &key, char const *value)
{
    if (!value)
        return;
    auto *ext = find(extension_id);
    if (!ext)
        return;
    auto *s = schema_for(*ext, key);
    if (!s || s->type != SettingType::Dropdown) {
        Logger::warn("Settings: set_string on '{}/{}' rejected (schema mismatch)", extension_id, key);
        return;
    }
    // Validate against option set if one is declared (non-empty).
    if (!s->options.empty()) {
        bool ok = false;
        for (SettingOption const &o : s->options) {
            if (o.id == value) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            Logger::warn("Settings: '{}/{}' value '{}' is not a valid option — ignored", extension_id, key, value);
            return;
        }
    }
    ensure_loaded(extension_id, *ext);
    std::string new_val(value);
    auto        it = ext->values.find(key);
    if (it != ext->values.end()) {
        if (auto *p = std::get_if<std::string>(&it->second); p && *p == new_val)
            return;
    }
    ext->values[key] = new_val;
    write_user_file(extension_id, *ext);
    refresh_abi_current_values(*ext);
    fire_change(extension_id, key, ext->values[key]);
}

// ---------------------------------------------------------------------------
// ABI cache (plain-C array seen by the proxy)
// ---------------------------------------------------------------------------

void SettingsManager::rebuild_abi_cache(ExtensionSettings &ext)
{
    ext.abi_cache.clear();
    ext.options_cache.clear();
    ext.abi_cache.reserve(ext.schema.size());
    ext.options_cache.reserve(ext.schema.size());

    for (SettingSchema const &s : ext.schema) {
        int8_t type;
        switch (s.type) {
        case SettingType::Toggle:   type = X4N_SETTING_TOGGLE;   break;
        case SettingType::Dropdown: type = X4N_SETTING_DROPDOWN; break;
        case SettingType::Slider:   type = X4N_SETTING_SLIDER;   break;
        default:                    type = -1;                   break;
        }

        // Options array for dropdowns — stored in parallel per-schema vector.
        std::vector<SettingOptionC> opts;
        opts.reserve(s.options.size());
        for (SettingOption const &o : s.options)
            opts.emplace_back(SettingOptionC{o.id.c_str(), o.text.c_str()});
        ext.options_cache.emplace_back(std::move(opts));
        auto const &stored = ext.options_cache.back();

        ext.abi_cache.emplace_back(SettingInfo{
            .id             = s.id.c_str(),
            .name           = s.name.c_str(),
            .options        = stored.empty() ? nullptr : stored.data(),
            .option_count   = static_cast<int>(stored.size()),
            .type           = type,
            .default_bool   = static_cast<uint8_t>(s.default_bool ? 1 : 0),
            .default_number = s.default_number,
            .default_string = s.default_string.c_str(),
            .min            = s.min,
            .max            = s.max,
            .step           = s.step,
        });
    }

    ext.abi_cache_built = true;
}

void SettingsManager::refresh_abi_current_values(ExtensionSettings &ext)
{
    if (!ext.abi_cache_built)
        rebuild_abi_cache(ext);
    for (size_t i = 0; i < ext.schema.size(); ++i) {
        SettingSchema const &s    = ext.schema[i];
        SettingInfo         &info = ext.abi_cache[i];

        auto it = ext.values.find(s.id);
        if (it == ext.values.end()) {
            // Missing → fall back to defaults.
            info.current_bool   = info.default_bool;
            info.current_number = info.default_number;
            info.current_string = info.default_string;
            continue;
        }
        std::visit(
            [&info, &s]<typename Ty>(Ty const &v) {
                using BaseT = std::decay_t<Ty>;
                if constexpr (std::is_same_v<BaseT, bool>) {
                    info.current_bool = v ? 1 : 0;
                } else if constexpr (std::is_same_v<BaseT, double>) {
                    info.current_number = v;
                } else if constexpr (std::is_same_v<BaseT, std::string>) {
                    // Map string value back to the stable option id pointer so the
                    // proxy sees a pointer owned by `schema` (immutable post-load).
                    //
                    // Invariant: `values[key]` for a Dropdown is always an id that
                    // exists in `s.options`. Enforced on every entry path:
                    //   - parse_one (schema default validation)
                    //   - ensure_loaded (user.json values validated against options)
                    //   - set_string  (rejects values not in options)
                    // If the lookup misses here the invariant has been broken.
                    char const *resolved = info.default_string; // safe fallback (also in schema)
                    for (SettingOption const &o : s.options) {
                        if (o.id == v) {
                            resolved = o.id.c_str();
                            break;
                        }
                    }
                    info.current_string = resolved;
                }
            },
            it->second
        );
    }
}

int SettingsManager::enumerate(std::string const &extension_id, SettingInfo const **out)
{
    if (out)
        *out = nullptr;
    auto *ext = find(extension_id);
    if (!ext || ext->schema.empty())
        return 0;
    ensure_loaded(extension_id, *ext);
    if (!ext->abi_cache_built)
        rebuild_abi_cache(*ext);
    refresh_abi_current_values(*ext);
    if (out)
        *out = ext->abi_cache.data();
    return static_cast<int>(ext->abi_cache.size());
}

void SettingsManager::set_from_abi(
    std::string const   &extension_id,
    std::string const   &key,
    SettingValueC const &value)
{
    switch (value.type) {
    case X4N_SETTING_TOGGLE:   set_bool(extension_id, key, value.b != 0);             break;
    case X4N_SETTING_SLIDER:   set_number(extension_id, key, value.d);                break;
    case X4N_SETTING_DROPDOWN: set_string(extension_id, key, value.s ? value.s : ""); break;
    default:
        Logger::warn("Settings: set_from_abi with unknown type={}", value.type);
        break;
    }
}

// ---------------------------------------------------------------------------
// Schema parsing (called from ExtensionManager::parse_config)
// ---------------------------------------------------------------------------

// Emit a parse-time warning to both the framework log and (if provided) a
// per-extension buffer that the caller will flush into the extension's own
// log once it's open.
template <typename... Args>
static void schema_warn(
    std::vector<std::pair<LogLevel, std::string>> *buf,
    std::format_string<Args...> fmt,
    Args &&...args)
{
    std::string msg = std::format(fmt, std::forward<Args>(args)...);
    Logger::warn("{}", msg);
    if (buf)
        buf->emplace_back(LogLevel::Warn, std::move(msg));
}

static bool parse_one(
    json const        &j,
    SettingSchema     &out,
    std::string const &context,
    std::vector<std::pair<LogLevel, std::string>> *warnings)
{
    if (!j.is_object()) {
        schema_warn(warnings, "Settings: {} entry is not an object", context);
        return false;
    }
    auto id = j.find("id");
    if (id == j.end() || !id->is_string()) {
        schema_warn(warnings, "Settings: {} entry missing 'id'", context);
        return false;
    }
    out.id   = id->get<std::string>();
    out.name = j.value("name", out.id);

    auto type_str = j.value("type", std::string{});
    if (type_str == "toggle") {
        out.type = SettingType::Toggle;
    } else if (type_str == "dropdown") {
        out.type = SettingType::Dropdown;
    } else if (type_str == "slider") {
        out.type = SettingType::Slider;
    } else {
        schema_warn(warnings, "Settings: {}/{} unknown type '{}' — skipping", context, out.id, type_str);
        return false;
    }

    json::const_iterator it;

    switch (out.type) {
    case SettingType::Toggle:
        it = j.find("default");
        if (it != j.end() && it->is_boolean())
            out.default_bool = it->get<bool>();
        break;

    case SettingType::Slider:
        it = j.find("min");
        if (it != j.end() && it->is_number())
            out.min = it->get<double>();
        it = j.find("max");
        if (it != j.end() && it->is_number())
            out.max = it->get<double>();
        it = j.find("step");
        if (it != j.end() && it->is_number())
            out.step = it->get<double>();
        if (out.max < out.min)
            std::swap(out.min, out.max);
        if (out.step <= 0)
            out.step = 1.0;
        it = j.find("default");
        if (it != j.end() && it->is_number())
            out.default_number = std::clamp(it->get<double>(), out.min, out.max);
        else
            out.default_number = out.min;
        break;

    case SettingType::Dropdown:
        it = j.find("options");
        if (it != j.end() && it->is_array()) {
            for (json const &o : *it) {
                if (!o.is_object())
                    continue;
                SettingOption opt;
                opt.id   = o.value("id",   std::string{});
                opt.text = o.value("text", opt.id);
                if (!opt.id.empty())
                    out.options.emplace_back(std::move(opt));
            }
        }
        if (out.options.empty()) {
            schema_warn(warnings, "Settings: {}/{} dropdown has no options — skipping",
                        context, out.id);
            return false;
        }

        it = j.find("default");
        if (it != j.end() && it->is_string()) {
            out.default_string = it->get<std::string>();
            bool ok = std::ranges::any_of(
                out.options, [&out](SettingOption const &o) { return o.id == out.default_string; }
            );
            if (!ok) {
                schema_warn(warnings, "Settings: {}/{} default '{}' not in options — using '{}'",
                            context, out.id, out.default_string, out.options.front().id);
                out.default_string = out.options.front().id;
            }
        } else {
            out.default_string = out.options.front().id;
        }
        break;
    }

    return true;
}

bool SettingsManager::parse_schema_array(
    json const                                    &node,
    std::vector<SettingSchema>                    &out,
    std::string const                             &context,
    std::vector<std::pair<LogLevel, std::string>> *warnings)
{
    out.clear();
    if (node.is_null())
        return true;
    if (!node.is_array()) {
        schema_warn(warnings, "Settings: {} 'settings' is not an array — ignoring", context);
        return false;
    }
    for (size_t i = 0; i < node.size(); ++i) {
        SettingSchema s;
        if (parse_one(node[i], s, context + "[" + std::to_string(i) + "]", warnings))
            out.emplace_back(std::move(s));
    }
    return true;
}

} // namespace x4n
