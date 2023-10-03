/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "win/meta.h"
#include "win/session_manager.h"
#include "win/space_qobject.h"
#include "win/util.h"
#include "win/window_qobject.h"
#include <win/subspace.h>

#include <KConfigGroup>
#include <QObject>
#include <unordered_map>

namespace KWin::input::xkb
{

template<typename T, typename U>
uint32_t get_layout(T const& layouts, U const& reference)
{
    auto it = layouts.find(reference);
    if (it == layouts.end()) {
        return 0;
    }
    return it->second;
}

class KWIN_EXPORT layout_policy_qobject : public QObject
{
public:
    ~layout_policy_qobject() override;
};

template<typename Manager>
class layout_policy
{
public:
    virtual ~layout_policy() = default;

    virtual QString name() const = 0;

    std::unique_ptr<layout_policy_qobject> qobject;
    Manager* manager;

    using window_t = typename std::decay_t<decltype(manager->redirect.space)>::window_t;

protected:
    explicit layout_policy(Manager* manager, KConfigGroup const& config = KConfigGroup())
        : qobject{std::make_unique<layout_policy_qobject>()}
        , manager(manager)
        , config(config)
    {
        QObject::connect(manager->qobject.get(),
                         &decltype(manager->qobject)::element_type::layoutsReconfigured,
                         qobject.get(),
                         [this] { clear_cache(); });
        QObject::connect(manager->qobject.get(),
                         &decltype(manager->qobject)::element_type::layoutChanged,
                         qobject.get(),
                         [this](auto index) { this->handle_layout_change(index); });
    }

    virtual void clear_cache() = 0;
    virtual void handle_layout_change(uint index) = 0;

    void set_layout(uint index)
    {
        get_keyboard()->switch_to_layout(index);
    }

    virtual QString const default_layout_entry_key() const
    {
        return QLatin1String(default_layout_entry_key_prefix) % name() % QLatin1Char('_');
    }

    void clear_layouts()
    {
        auto const layout_entries = config.keyList().filter(default_layout_entry_key_prefix);
        for (const auto& entry : layout_entries) {
            config.deleteEntry(entry);
        }
    }

    auto get_keyboard()
    {
        return xkb::get_primary_xkb_keyboard(this->manager->redirect.platform);
    }

    KConfigGroup config;
    constexpr static char default_layout_entry_key_prefix[]{"LayoutDefault"};
};

template<typename Manager>
class global_layout_policy : public layout_policy<Manager>
{
public:
    global_layout_policy(Manager* manager, KConfigGroup const& config)
        : layout_policy<Manager>(manager, config)
    {
        auto session_manager = manager->redirect.space.session_manager.get();
        QObject::connect(session_manager,
                         &win::session_manager::prepareSessionSaveRequested,
                         this->qobject.get(),
                         [this] {
                             this->clear_layouts();
                             if (auto const layout = this->get_keyboard()->layout) {
                                 this->config.writeEntry(default_layout_entry_key(), layout);
                             }
                         });

        QObject::connect(session_manager,
                         &win::session_manager::loadSessionRequested,
                         this->qobject.get(),
                         [this] {
                             if (this->get_keyboard()->layouts_count() > 1) {
                                 this->set_layout(
                                     this->config.readEntry(default_layout_entry_key(), 0));
                             }
                         });
    }

    QString name() const override
    {
        return QStringLiteral("Global");
    }

protected:
    void clear_cache() override
    {
    }

    void handle_layout_change(uint index) override
    {
        Q_UNUSED(index)
    }

private:
    QString const default_layout_entry_key() const override
    {
        return QLatin1String(this->default_layout_entry_key_prefix) % name();
    }
};

template<typename Manager>
class subspace_layout_policy : public layout_policy<Manager>
{
public:
    subspace_layout_policy(Manager* manager, KConfigGroup const& config)
        : layout_policy<Manager>(manager, config)
    {
        auto& space = manager->redirect.space;
        QObject::connect(space.subspace_manager->qobject.get(),
                         &decltype(space.subspace_manager->qobject)::element_type::current_changed,
                         this->qobject.get(),
                         [this] { handle_desktop_change(); });

        auto session_manager = space.session_manager.get();
        QObject::connect(session_manager,
                         &win::session_manager::prepareSessionSaveRequested,
                         this->qobject.get(),
                         [this] {
                             this->clear_layouts();

                             for (auto const& [subspace, layout] : layouts) {
                                 if (!layout) {
                                     continue;
                                 }

                                 this->config.writeEntry(this->default_layout_entry_key()
                                                             % QLatin1String(QByteArray::number(
                                                                 subspace->x11DesktopNumber())),
                                                         layout);
                             }
                         });

        QObject::connect(
            session_manager,
            &win::session_manager::loadSessionRequested,
            this->qobject.get(),
            [this] {
                if (this->get_keyboard()->layouts_count() > 1) {
                    auto const& subspaces
                        = this->manager->redirect.space.subspace_manager->subspaces();

                    for (auto const subspace : subspaces) {
                        uint const layout = this->config.readEntry(
                            this->default_layout_entry_key()
                                % QLatin1String(QByteArray::number(subspace->x11DesktopNumber())),
                            0u);

                        if (layout) {
                            layouts.insert({subspace, layout});
                            QObject::connect(subspace,
                                             &win::subspace::aboutToBeDestroyed,
                                             this->qobject.get(),
                                             [this, subspace] { layouts.erase(subspace); });
                        }
                    }

                    handle_desktop_change();
                }
            });
    }

    QString name() const override
    {
        return QStringLiteral("Desktop");
    }

protected:
    void clear_cache() override
    {
        layouts.clear();
    }

    void handle_layout_change(uint index) override
    {
        auto desktop = this->manager->redirect.space.subspace_manager->current_subspace();
        if (!desktop) {
            return;
        }

        auto it = layouts.find(desktop);

        if (it == layouts.end()) {
            layouts.insert({desktop, index});
            QObject::connect(desktop,
                             &win::subspace::aboutToBeDestroyed,
                             this->qobject.get(),
                             [this, desktop] { layouts.erase(desktop); });
        } else {
            it->second = index;
        }
    }

private:
    void handle_desktop_change()
    {
        if (auto desktop = this->manager->redirect.space.subspace_manager->current_subspace()) {
            this->set_layout(get_layout(layouts, desktop));
        }
    }

    std::unordered_map<win::subspace*, uint32_t> layouts;
};

template<typename Manager>
class window_layout_policy : public layout_policy<Manager>
{
public:
    using window_t = typename layout_policy<Manager>::window_t;

    explicit window_layout_policy(Manager* manager)
        : layout_policy<Manager>(manager)
    {
        QObject::connect(manager->redirect.space.qobject.get(),
                         &win::space_qobject::clientActivated,
                         this->qobject.get(),
                         [this] {
                             auto window = this->manager->redirect.space.stacking.active;
                             if (!window) {
                                 return;
                             }

                             std::visit(overload{[&](auto&& win) {
                                            // Ignore some special types.
                                            if (win::is_desktop(win) || win::is_dock(win)) {
                                                return;
                                            }

                                            this->set_layout(get_layout(layouts, window_t(win)));
                                        }},
                                        *window);
                         });
    }

    QString name() const override
    {
        return QStringLiteral("Window");
    }

protected:
    void clear_cache() override
    {
        layouts.clear();
    }

    void handle_layout_change(uint index) override
    {
        auto window = this->manager->redirect.space.stacking.active;
        if (!window) {
            return;
        }

        std::visit(overload{[this, index](auto&& win) {
                       // Ignore some special types.
                       if (win::is_desktop(win) || win::is_dock(win)) {
                           return;
                       }

                       if (auto it = layouts.find(window_t(win)); it != layouts.end()) {
                           it->second = index;
                           return;
                       }

                       layouts.insert({window_t(win), index});
                       QObject::connect(win->qobject.get(),
                                        &win::window_qobject::closed,
                                        this->qobject.get(),
                                        [this, win] { layouts.erase(window_t(win)); });
                   }},
                   *window);
    }

private:
    std::unordered_map<window_t, uint32_t> layouts;
};

template<typename Manager>
class application_layout_policy : public layout_policy<Manager>
{
public:
    using window_t = typename layout_policy<Manager>::window_t;

    application_layout_policy(Manager* manager, KConfigGroup const& config)
        : layout_policy<Manager>(manager, config)
    {
        auto& space = manager->redirect.space;
        QObject::connect(space.qobject.get(),
                         &win::space_qobject::clientActivated,
                         this->qobject.get(),
                         [this, &space] {
                             if (auto act = space.stacking.active) {
                                 std::visit(
                                     overload{[this](auto&& act) { handle_client_activated(act); }},
                                     *act);
                             }
                         });

        auto session_manager = space.session_manager.get();
        QObject::connect(
            session_manager,
            &win::session_manager::prepareSessionSaveRequested,
            this->qobject.get(),
            [this] {
                this->clear_layouts();

                for (auto const& [win, layout] : layouts) {
                    if (!layout) {
                        continue;
                    }

                    auto name = std::visit(
                        overload{[](auto&& win) { return win->control->desktop_file_name; }}, win);
                    if (!name.isEmpty()) {
                        this->config.writeEntry(
                            this->default_layout_entry_key() % QLatin1String(name), layout);
                    }
                }
            });
        QObject::connect(session_manager,
                         &win::session_manager::loadSessionRequested,
                         this->qobject.get(),
                         [this] {
                             if (this->get_keyboard()->layouts_count() > 1) {
                                 auto const keyPrefix = this->default_layout_entry_key();
                                 auto const keyList = this->config.keyList().filter(keyPrefix);
                                 for (auto const& key : keyList) {
                                     restored_layouts.insert(
                                         {QStringView(key).mid(keyPrefix.size()).toLatin1(),
                                          this->config.readEntry(key, 0)});
                                 }
                             }
                         });
    }

    QString name() const override
    {
        return QStringLiteral("WinClass");
    }

protected:
    void clear_cache() override
    {
        layouts.clear();
    }

    void handle_layout_change(uint index) override
    {
        auto window = this->manager->redirect.space.stacking.active;
        if (!window) {
            return;
        }

        std::visit(overload{[this, index](auto&& window) {
                       // Ignore some special types.
                       if (win::is_desktop(window) || win::is_dock(window)) {
                           return;
                       }

                       auto it = layouts.find(window_t(window));

                       if (it == layouts.end()) {
                           layouts.insert({window_t(window), index});
                           QObject::connect(window->qobject.get(),
                                            &win::window_qobject::closed,
                                            this->qobject.get(),
                                            [this, window] { layouts.erase(window_t(window)); });
                       } else {
                           if (it->second == index) {
                               return;
                           }
                           it->second = index;
                       }

                       // Update all layouts for the application.
                       for (auto& [win, layout] : layouts) {
                           if (std::visit(overload{[&](auto&& win) {
                                              return win::belong_to_same_client(win, window);
                                          }},
                                          win)) {
                               layout = index;
                           }
                       }
                   }},
                   *window);
    }

private:
    template<typename Win>
    void handle_client_activated(Win* window)
    {
        // Ignore some special types.
        if (win::is_desktop(window) || win::is_dock(window)) {
            return;
        }

        auto it = layouts.find(window);
        if (it != layouts.end()) {
            this->set_layout(it->second);
            return;
        }

        for (auto const& [win, layout] : layouts) {
            if (std::visit(
                    overload{[&](auto&& win) { return win::belong_to_same_client(window, win); }},
                    win)) {
                this->set_layout(layout);
                handle_layout_change(layout);
                return;
            }
        }

        auto restored_layout = 0;

        if (auto restored_it = restored_layouts.find(window->control->desktop_file_name);
            restored_it != restored_layouts.end()) {
            restored_layout = restored_it->second;
            restored_layouts.erase(restored_it);
        }

        this->set_layout(restored_layout);

        if (auto index = this->get_keyboard()->layout) {
            handle_layout_change(index);
        }
    }

    std::unordered_map<window_t, uint32_t> layouts;
    std::unordered_map<QByteArray, uint32_t> restored_layouts;
};

template<typename Manager>
std::unique_ptr<layout_policy<Manager>>
create_layout_policy(Manager* manager, KConfigGroup const& config, QString const& policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return std::make_unique<subspace_layout_policy<Manager>>(manager, config);
    }
    if (policy.toLower() == QStringLiteral("window")) {
        return std::make_unique<window_layout_policy<Manager>>(manager);
    }
    if (policy.toLower() == QStringLiteral("winclass")) {
        return std::make_unique<application_layout_policy<Manager>>(manager, config);
    }
    return std::make_unique<global_layout_policy<Manager>>(manager, config);
}

}
