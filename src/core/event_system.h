#pragma once
// ---------------------------------------------------------------------------
// Event System — Publish / Subscribe
//
// Thread-safe event bus. Extensions subscribe during x4native_init();
// events are dispatched by the core DLL or forwarded from Lua.
// ---------------------------------------------------------------------------

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace x4n {

using EventCallback = void(*)(const char* event_name, void* data, void* userdata);

class EventSystem {
public:
    static void init();
    static void shutdown();

    /// Subscribe to an event. Returns a unique subscription id.
    static int subscribe(const std::string& event_name,
                         EventCallback callback,
                         void* userdata = nullptr);

    /// Remove a subscription by id.
    static void unsubscribe(int id);

    /// Dispatch an event to all subscribers.
    static void fire(const char* event_name, void* data = nullptr);

private:
    struct Subscription {
        int            id;
        EventCallback  callback;
        void*          userdata;
    };

    static std::unordered_map<std::string, std::vector<Subscription>> s_subscribers;
    static std::mutex s_mutex;
    static int        s_next_id;
};

} // namespace x4n
