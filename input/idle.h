/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "singleton_interface.h"

#include <QObject>
#include <QTimer>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace KWin::input
{

struct idle_listener {
    using callback = std::function<void()>;

    idle_listener(std::chrono::milliseconds time_to_idle, callback idle, callback resume)
        : time_to_idle{time_to_idle}
        , idle{idle}
        , resume{resume}
    {
    }

    // Must be non-negative.
    std::chrono::milliseconds time_to_idle{std::chrono::seconds(5)};

    // Callbacks are optional. May be set depending on which events the listener is interested in.
    callback idle;
    callback resume;
};

class idle_qobject : public QObject
{
public:
    using listener_setup = std::function<void(idle_listener&)>;

    idle_qobject(listener_setup reg, listener_setup unreg, std::function<void()> sim)
        : register_listener_impl{std::move(reg)}
        , unregister_listener_impl{std::move(unreg)}
        , simulate_activity_impl{std::move(sim)}
    {
    }

    void register_listener(idle_listener& listener)
    {
        register_listener_impl(listener);
    }

    void unregister_listener(idle_listener& listener)
    {
        unregister_listener_impl(listener);
    }

    void simulate_activity()
    {
        simulate_activity_impl();
    }

private:
    friend class idle;
    listener_setup register_listener_impl;
    listener_setup unregister_listener_impl;
    std::function<void()> simulate_activity_impl;
};

class idle
{
public:
    idle()
        : qobject{std::make_unique<idle_qobject>(
            [this](auto& listener) { add_listener(listener); },
            [this](auto& listener) { remove_listener(listener); },
            [this] { report_activity(); })}
        , countdown{std::make_unique<QTimer>()}
    {
        singleton_interface::idle_qobject = qobject.get();

        countdown->setSingleShot(true);
        QObject::connect(
            countdown.get(), &QTimer::timeout, qobject.get(), [this] { handle_countdown(); });
    }

    idle(idle const&) = delete;
    idle& operator=(idle const&) = delete;
    idle(idle&& other) noexcept
    {
        *this = std::move(other);
    }

    idle& operator=(idle&& other) noexcept
    {
        qobject = std::move(other.qobject);
        listeners = std::move(other.listeners);
        inhibit_count = other.inhibit_count;

        countdown = std::move(other.countdown);
        QObject::disconnect(countdown.get(), &QTimer::timeout, qobject.get(), nullptr);

        countdown_sum = other.countdown_sum;
        QObject::connect(
            countdown.get(), &QTimer::timeout, qobject.get(), [this] { handle_countdown(); });

        qobject->register_listener_impl = [this](auto& listener) { add_listener(listener); };
        qobject->unregister_listener_impl = [this](auto& listener) { add_listener(listener); };
        qobject->simulate_activity_impl = [this] { report_activity(); };

        return *this;
    }

    ~idle()
    {
        if (singleton_interface::idle_qobject == qobject.get()) {
            singleton_interface::idle_qobject = nullptr;
        }
    }

    void add_listener(idle_listener& listener)
    {
        assert(listener.time_to_idle >= std::chrono::milliseconds::zero());

        if (!countdown->isActive()) {
            listener_map_set_insert(listener, listeners.waiting, listener.time_to_idle);
            if (!inhibit_count) {
                countdown->start(listener.time_to_idle);
            }
            return;
        }

        // When the countdown is active we are guaranteed to not be inhibited.
        assert(!inhibit_count);

        auto const timer_residue = countdown->remainingTimeAsDuration();
        auto const timer_elapsed = countdown->intervalAsDuration() - timer_residue;

        if (timer_residue >= listener.time_to_idle) {
            listener_map_set_insert(listener, listeners.waiting, listener.time_to_idle);
            countdown_sum += timer_elapsed;
            countdown->start(listener.time_to_idle);
        } else {
            // We need to offset our first wait time with the elapsed time so we wait long enough.
            auto const wait_time = listener.time_to_idle + timer_elapsed;
            listener_map_set_insert(listener, listeners.waiting, wait_time);
            listeners.splice.insert(&listener);
        }
    }

    void remove_listener(idle_listener& listener)
    {
        if (auto it = listeners.served.find(&listener); it != listeners.served.end()) {
            // Listener had already idled out.
            listeners.served.erase(it);
            return;
        }

        listener_map_set_remove(listener, listeners.waiting);

        if (listeners.waiting.empty()) {
            countdown->stop();
        }
    }

    void report_activity()
    {
        for (auto listener : listeners.served) {
            if (listener->resume) {
                listener->resume();
            }

            // We move over listeners with 0 idle time too so we don't report activity multiple
            // times to these in one procesing run.
            listener_map_set_insert(*listener, listeners.waiting, listener->time_to_idle);
        }

        listeners.served.clear();

        unset_countdown();

        if (!listeners.waiting.empty() && !inhibit_count) {
            countdown->start(listeners.waiting.begin()->first);
        }
    }

    void handle_countdown()
    {
        assert(!inhibit_count);

        auto idle_next_cohort = [this](auto& container) {
            assert(!container.empty());
            auto interval = container.begin()->first;
            for (auto listener : container.begin()->second) {
                if (listener->idle) {
                    listener->idle();
                }
            }

            for (auto listener : container.begin()->second) {
                listeners.served.insert(listener);
            }

            container.erase(container.begin());
            return interval;
        };

        idle_next_cohort(listeners.waiting);

        if (listeners.waiting.empty()) {
            countdown_sum = {};
            return;
        }

        auto const last_interval = countdown->intervalAsDuration();
        countdown_sum += last_interval;

        auto const wait_time = listeners.waiting.begin()->first - last_interval;
        assert(wait_time > std::chrono::milliseconds::zero());
        countdown->start(wait_time);
    }

    void inhibit()
    {
        if (++inhibit_count == 1) {
            unset_countdown();
        }
    }

    void uninhibit()
    {
        assert(!countdown->isActive());

        if (--inhibit_count > 0) {
            // Still inhibited.
            return;
        }

        assert(inhibit_count == 0);

        if (!listeners.waiting.empty()) {
            countdown->start(listeners.waiting.begin()->first);
        }
    }

    std::unique_ptr<idle_qobject> qobject;

    uint32_t inhibit_count{0};

private:
    using listeners_map_set = std::map<std::chrono::milliseconds, std::set<idle_listener*>>;

    void listener_map_set_insert(idle_listener& listener,
                                 listeners_map_set& map,
                                 std::chrono::milliseconds const& key)
    {
        if (auto it = map.find(key); it != map.end()) {
            it->second.insert(&listener);
        } else {
            map.insert({key, {&listener}});
        }
    }

    bool listener_map_set_remove(idle_listener& listener, listeners_map_set& map)
    {
        for (auto& [time, set] : map) {
            if (auto it = set.find(&listener); it != set.end()) {
                set.erase(it);
                if (set.empty()) {
                    map.erase(time);
                }
                return true;
            }
        }

        return true;
    }

    void unset_countdown()
    {
        for (auto listener : listeners.splice) {
            // Need to bring spliced listeners into their actual cohort.
            listener_map_set_remove(*listener, listeners.waiting);
            listener_map_set_insert(*listener, listeners.waiting, listener->time_to_idle);
        }

        listeners.splice.clear();
        countdown_sum = {};
        countdown->stop();
    }

    struct {
        std::set<idle_listener*> splice;
        listeners_map_set waiting;
        std::set<idle_listener*> served;
    } listeners;

    std::unique_ptr<QTimer> countdown;
    std::chrono::milliseconds countdown_sum{};
};

}
