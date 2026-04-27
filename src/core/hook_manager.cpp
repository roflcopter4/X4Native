// ---------------------------------------------------------------------------
// x4native_core.dll — Hook Manager Implementation
//
// Uses MinHook for inline hooking. Each hooked game function gets one
// MinHook detour that routes through our managed dispatcher, which runs
// before/after callback chains with SEH protection.
// ---------------------------------------------------------------------------

#include "Common.h"
#include "hook_manager.h"
#include "logger.h"
#include "game_api.h"

#include <MinHook.h>
#include <algorithm>
#include <ranges>

#if defined MH_ALL_HOOKS && MH_ALL_HOOKS == 0
# undef MH_ALL_HOOKS
# define MH_ALL_HOOKS nullptr
#endif


namespace x4n {

std::unordered_map<std::string, HookedFunction> HookManager::s_hooks;
std::recursive_mutex HookManager::s_mutex;
int  HookManager::s_next_id     = 1;
bool HookManager::s_initialized = false;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool HookManager::init()
{
    std::scoped_lock lock(s_mutex);
    if (s_initialized)
        return true;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        Logger::error("HookManager: MH_Initialize failed: {}", MH_StatusToString(status));
        return false;
    }

    s_initialized = true;
    Logger::info("HookManager: initialized");
    return true;
}

void HookManager::shutdown()
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized)
        return;

    // Disable and remove all hooks
    for (auto const &hf : s_hooks | std::views::values) {
        MH_DisableHook(hf.target);
        MH_RemoveHook(hf.target);
    }
    s_hooks.clear();

    MH_Uninitialize();
    s_initialized = false;
    Logger::info("HookManager: shut down");
}

void HookManager::remove_all()
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized)
        return;

    MH_DisableHook(MH_ALL_HOOKS);
    for (auto const &hf : s_hooks | std::views::values)
        MH_RemoveHook(hf.target);
    s_hooks.clear();
    Logger::info("HookManager: all hooks removed");
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

int HookManager::hook_before(
    char const    *function_name,
    X4HookCallback callback,
    void          *userdata,
    int            priority,
    char const    *ext_name)
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized || !function_name || !callback)
        return -1;

    HookedFunction *hf = ensure_hooked(function_name);
    if (!hf)
        return -1;

    int id = s_next_id++;

    hf->before_hooks.emplace_back(HookCallbackInfo{
        .id             = id,
        .priority       = priority,
        .extension_name = ext_name ? ext_name : "",
        .callback       = callback,
        .userdata       = userdata,
        .enabled        = true,
        .is_before      = true,
    });
    sort_callbacks(hf->before_hooks);

    Logger::debug("HookManager: before-hook #{} on '{}' (ext={}, pri={})", id, function_name, ext_name ? ext_name : "?", priority);
    return id;
}

int HookManager::hook_after(
    char const    *function_name,
    X4HookCallback callback,
    void          *userdata,
    int            priority,
    char const    *ext_name)
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized || !function_name || !callback)
        return -1;

    HookedFunction *hf = ensure_hooked(function_name);
    if (!hf)
        return -1;

    int id = s_next_id++;
    hf->after_hooks.push_back({id, priority, ext_name ? ext_name : "", callback, userdata, true, false});
    sort_callbacks(hf->after_hooks);

    Logger::debug("HookManager: after-hook #{} on '{}' (ext={}, pri={})", id, function_name, ext_name ? ext_name : "?", priority);
    return id;
}

void HookManager::unhook(int hook_id)
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized || hook_id <= 0)
        return;

    for (auto &[name, hf] : s_hooks) {
        auto remove_from = [&](std::vector<HookCallbackInfo> &cbs) -> bool {
            for (auto it = cbs.begin(); it != cbs.end(); ++it) {
                if (it->id == hook_id) {
                    Logger::debug("HookManager: removed hook #{} from '{}'", hook_id, name);
                    cbs.erase(it);
                    return true;
                }
            }
            return false;
        };
        if (remove_from(hf.before_hooks) || remove_from(hf.after_hooks)) {
            // If no callbacks remain, remove the MinHook detour
            if (hf.before_hooks.empty() && hf.after_hooks.empty()) {
                MH_DisableHook(hf.target);
                MH_RemoveHook(hf.target);
                s_hooks.erase(name);
                Logger::debug("HookManager: detour removed for '{}' (no callbacks left)", name);
            }
            return;
        }
    }
}

void HookManager::remove_all_for_extension(char const *ext_name)
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized || !ext_name)
        return;

    std::string en(ext_name);
    std::vector<std::string> empty_hooks;

    for (auto &[name, hf] : s_hooks) {
        auto remove_matching = [&en](std::vector<HookCallbackInfo> &cbs) {
            std::erase_if(cbs, [&en](HookCallbackInfo const &cb) { return cb.extension_name == en; });
        };
        remove_matching(hf.before_hooks);
        remove_matching(hf.after_hooks);

        if (hf.before_hooks.empty() && hf.after_hooks.empty())
            empty_hooks.push_back(name);
    }

    for (std::string const &name : empty_hooks) {
        auto it = s_hooks.find(name);
        if (it != s_hooks.end()) {
            MH_DisableHook(it->second.target);
            MH_RemoveHook(it->second.target);
            s_hooks.erase(it);
        }
    }
}

void *HookManager::get_trampoline(char const *function_name)
{
    std::scoped_lock lock(s_mutex);
    auto it = s_hooks.find(function_name);
    if (it != s_hooks.end())
        return it->second.trampoline;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Internal: ensure a MinHook detour is installed for a function
// ---------------------------------------------------------------------------
// Note: s_mutex must be held by caller
HookedFunction *HookManager::ensure_hooked(char const *function_name)
{
    // Already hooked?
    auto it = s_hooks.find(function_name);
    if (it != s_hooks.end())
        return &it->second;

    // Resolve the function address — try exports first, then internal RVA db
    void *target = GameAPI::get_function(function_name);
    if (!target)
        target = GameAPI::get_internal(function_name);
    if (!target) {
        Logger::error("HookManager: function '{}' not found (exports or internal DB)", function_name);
        return nullptr;
    }

    // Install MinHook detour — but we can't use a generic dispatcher directly
    // because MinHook replaces the function entry point with a jump to our
    // detour. The detour must have the same calling convention and signature.
    // The typed SDK wrappers (x4native.h) generate per-function detours and
    // pass them to ensure_detour() which calls MH_CreateHook.

    HookedFunction hf;
    hf.name       = function_name;
    hf.target     = target;
    hf.trampoline = nullptr;

    auto [ins, _] = s_hooks.emplace(function_name, std::move(hf));
    return &ins->second;
}

void HookManager::sort_callbacks(std::vector<HookCallbackInfo> &cbs)
{
    std::ranges::sort(
        cbs, [](HookCallbackInfo const &a, HookCallbackInfo const &b) { return a.priority < b.priority; });
}

// ---------------------------------------------------------------------------
// Detour installation — called by SDK typed wrappers via _ensure_detour
// ---------------------------------------------------------------------------

void *HookManager::ensure_detour(char const *function_name, void *detour_fn)
{
    std::scoped_lock lock(s_mutex);
    if (!s_initialized || !function_name || !detour_fn)
        return nullptr;

    HookedFunction *hf = ensure_hooked(function_name);
    if (!hf)
        return nullptr;

    // Already detoured — return existing trampoline
    if (hf->trampoline)
        return hf->trampoline;

    void     *trampoline = nullptr;
    MH_STATUS status     = MH_CreateHook(hf->target, detour_fn, &trampoline);
    if (status != MH_OK) {
        Logger::error("HookManager: MH_CreateHook failed for '{}': {}", function_name, MH_StatusToString(status));
        return nullptr;
    }

    status = MH_EnableHook(hf->target);
    if (status != MH_OK) {
        Logger::error("HookManager: MH_EnableHook failed for '{}': {}", function_name, MH_StatusToString(status));
        MH_RemoveHook(hf->target);
        return nullptr;
    }

    hf->trampoline = trampoline;
    Logger::info("HookManager: detour installed for '{}'", function_name);
    return trampoline;
}

// ---------------------------------------------------------------------------
// Dispatch — called by typed detours generated in x4native.h
// ---------------------------------------------------------------------------

// SEH wrapper — must be a separate function (no C++ objects needing unwind)
static int seh_call_hook(X4HookCallback callback, X4HookContext *ctx)
{
    __try {
        return callback(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void HookManager::run_before_hooks(X4HookContext *ctx)
{
    if (!ctx || !ctx->function_name)
        return;

    std::vector<HookCallbackInfo> hooks_copy;
    {
        std::scoped_lock lock(s_mutex);
        auto it = s_hooks.find(ctx->function_name);
        if (it == s_hooks.end())
            return;
        hooks_copy = it->second.before_hooks;
    }

    for (HookCallbackInfo const &hook : hooks_copy) {
        if (!hook.enabled)
            continue;
        ctx->userdata = hook.userdata;
        if (seh_call_hook(hook.callback, ctx) == -1) {
            Logger::error("HookManager: before-hook #{} crashed in '{}' (ext={}) — disabling", hook.id, ctx->function_name, hook.extension_name);
            // Disable in the actual chain
            std::scoped_lock lock(s_mutex);
            auto it = s_hooks.find(ctx->function_name);
            if (it != s_hooks.end()) {
                for (HookCallbackInfo &h : it->second.before_hooks)
                    if (h.id == hook.id) {
                        h.enabled = false;
                        break;
                    }
            }
        }
    }
}

void HookManager::run_after_hooks(X4HookContext *ctx)
{
    if (!ctx || !ctx->function_name)
        return;

    std::vector<HookCallbackInfo> hooks_copy;
    {
        std::scoped_lock lock(s_mutex);
        auto it = s_hooks.find(ctx->function_name);
        if (it == s_hooks.end())
            return;
        hooks_copy = it->second.after_hooks;
    }

    for (auto &hook : hooks_copy) {
        if (!hook.enabled)
            continue;
        ctx->userdata = hook.userdata;
        if (seh_call_hook(hook.callback, ctx) == -1) {
            Logger::error("HookManager: after-hook #{} crashed in '{}' (ext={}) — disabling", hook.id, ctx->function_name, hook.extension_name);
            std::scoped_lock lock(s_mutex);
            auto it = s_hooks.find(ctx->function_name);
            if (it != s_hooks.end()) {
                for (auto &h : it->second.after_hooks)
                    if (h.id == hook.id) {
                        h.enabled = false;
                        break;
                    }
            }
        }
    }
}

} // namespace x4n
