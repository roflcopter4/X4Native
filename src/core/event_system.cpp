#include "Common.h"
#include "event_system.h"
#include "logger.h"

#include <algorithm>
#include <ranges>


namespace x4n {

std::unordered_map<std::string, std::vector<EventSystem::Subscription>>      EventSystem::s_subscribers;
std::array<std::vector<EventSystem::Subscription>, EventSystem::MAX_MD_TYPE> EventSystem::s_md_before;
std::array<std::vector<EventSystem::Subscription>, EventSystem::MAX_MD_TYPE> EventSystem::s_md_after;
std::mutex EventSystem::s_mutex;
int        EventSystem::s_next_id = 1;

void EventSystem::init()
{
    std::scoped_lock lock(s_mutex);
    s_subscribers.clear();
    for (auto &v : s_md_before)
        v.clear();
    for (auto &v : s_md_after)
        v.clear();
    s_next_id = 1;
}

void EventSystem::shutdown()
{
    std::scoped_lock lock(s_mutex);
    s_subscribers.clear();
    for (auto &v : s_md_before)
        v.clear();
    for (auto &v : s_md_after)
        v.clear();
}

int EventSystem::subscribe(char const *event_name, EventCallback callback, void *userdata)
{
    std::scoped_lock lock(s_mutex);
    int id = s_next_id++;
    s_subscribers[event_name].emplace_back(Subscription{id, callback, userdata});
    Logger::debug("Event '{}': subscribed (id={})", event_name, id);
    return id;
}

void EventSystem::unsubscribe(int id)
{
    std::scoped_lock lock(s_mutex);
    // Check named events
    for (auto &subs : s_subscribers | std::views::values)
        std::erase_if(subs, [id](Subscription const &s) { return s.id == id; });

    // Check MD event arrays
    auto remove_from = [id](auto &arr) {
        for (auto &v : arr)
            std::erase_if(v, [id](Subscription const &s) { return s.id == id; });
    };
    remove_from(s_md_before);
    remove_from(s_md_after);
}

int EventSystem::md_subscribe_before(uint32_t type_id, EventCallback callback, void *userdata)
{
    if (type_id >= MAX_MD_TYPE || !callback)
        return -1;
    std::scoped_lock lock(s_mutex);
    int id = s_next_id++;
    s_md_before[type_id].emplace_back(Subscription{id, callback, userdata});
    Logger::debug("MD event {}: subscribed before (id={})", type_id, id);
    return id;
}

int EventSystem::md_subscribe_after(uint32_t type_id, EventCallback callback, void *userdata)
{
    if (type_id >= MAX_MD_TYPE || !callback)
        return -1;
    std::scoped_lock lock(s_mutex);
    int id = s_next_id++;
    s_md_after[type_id].push_back({id, callback, userdata});
    Logger::debug("MD event {}: subscribed after (id={})", type_id, id);
    return id;
}

void EventSystem::md_fire_before(uint32_t type_id, void *payload)
{
    if (type_id >= MAX_MD_TYPE)
        return;
    std::vector<Subscription> snapshot;
    {
        std::scoped_lock lock(s_mutex);
        auto &subs = s_md_before[type_id];
        if (subs.empty())
            return;
        snapshot = subs;
    }
    for (Subscription &sub : snapshot)
        sub.callback(nullptr, payload, sub.userdata);
}

void EventSystem::md_fire_after(uint32_t type_id, void *payload)
{
    if (type_id >= MAX_MD_TYPE)
        return;
    std::vector<Subscription> snapshot;
    {
        std::scoped_lock lock(s_mutex);
        auto &subs = s_md_after[type_id];
        if (subs.empty())
            return;
        snapshot = subs;
    }
    for (Subscription &sub : snapshot)
        sub.callback(nullptr, payload, sub.userdata);
}

void EventSystem::fire(char const *event_name, void *data)
{
    // Copy subscriber list before dispatching so callbacks can safely
    // subscribe/unsubscribe without deadlocking.
    std::vector<Subscription> snapshot;
    {
        std::scoped_lock lock(s_mutex);
        auto it = s_subscribers.find(event_name);
        if (it == s_subscribers.end() || it->second.empty())
            return;
        snapshot = it->second;
    }

    for (Subscription &sub : snapshot)
        sub.callback(event_name, data, sub.userdata);
}

} // namespace x4n
