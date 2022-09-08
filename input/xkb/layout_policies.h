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
#include "win/virtual_desktops.h"

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

    using window_t = typename decltype(manager->xkb.platform->base.space)::element_type::window_t;

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
        return xkb::get_primary_xkb_keyboard(*this->manager->xkb.platform);
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
        auto session_manager = manager->xkb.platform->base.space->session_manager.get();
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
class virtual_desktop_layout_policy : public layout_policy<Manager>
{
public:
    virtual_desktop_layout_policy(Manager* manager, KConfigGroup const& config)
        : layout_policy<Manager>(manager, config)
    {
        auto& space = *manager->xkb.platform->base.space;
        QObject::connect(space.virtual_desktop_manager->qobject.get(),
                         &win::virtual_desktop_manager_qobject::currentChanged,
                         this->qobject.get(),
                         [this] { handle_desktop_change(); });

        auto session_manager = space.session_manager.get();
        QObject::connect(session_manager,
                         &win::session_manager::prepareSessionSaveRequested,
                         this->qobject.get(),
                         [this] {
                             this->clear_layouts();

                             for (auto const& [vd, layout] : layouts) {
                                 if (!layout) {
                                     continue;
                                 }

                                 this->config.writeEntry(this->default_layout_entry_key()
                                                             % QLatin1String(QByteArray::number(
                                                                 vd->x11DesktopNumber())),
                                                         layout);
                             }
                         });

        QObject::connect(
            session_manager,
            &win::session_manager::loadSessionRequested,
            this->qobject.get(),
            [this] {
                if (this->get_keyboard()->layouts_count() > 1) {
                    auto const& desktops = this->manager->xkb.platform->base.space
                                               ->virtual_desktop_manager->desktops();

                    for (auto const desktop : desktops) {
                        uint const layout = this->config.readEntry(
                            this->default_layout_entry_key()
                                % QLatin1String(QByteArray::number(desktop->x11DesktopNumber())),
                            0u);

                        if (layout) {
                            layouts.insert({desktop, layout});
                            QObject::connect(desktop,
                                             &win::virtual_desktop::aboutToBeDestroyed,
                                             this->qobject.get(),
                                             [this, desktop] { layouts.erase(desktop); });
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
        auto desktop
            = this->manager->xkb.platform->base.space->virtual_desktop_manager->currentDesktop();
        if (!desktop) {
            return;
        }

        auto it = layouts.find(desktop);

        if (it == layouts.end()) {
            layouts.insert({desktop, index});
            QObject::connect(desktop,
                             &win::virtual_desktop::aboutToBeDestroyed,
                             this->qobject.get(),
                             [this, desktop] { layouts.erase(desktop); });
        } else {
            it->second = index;
        }
    }

private:
    void handle_desktop_change()
    {
        if (auto desktop
            = this->manager->xkb.platform->base.space->virtual_desktop_manager->currentDesktop()) {
            this->set_layout(get_layout(layouts, desktop));
        }
    }

    std::unordered_map<win::virtual_desktop*, uint32_t> layouts;
};

template<typename Manager>
class window_layout_policy : public layout_policy<Manager>
{
public:
    using window_t = typename layout_policy<Manager>::window_t;

    explicit window_layout_policy(Manager* manager)
        : layout_policy<Manager>(manager)
    {
        QObject::connect(manager->xkb.platform->base.space->qobject.get(),
                         &win::space_qobject::clientActivated,
                         this->qobject.get(),
                         [this] {
                             auto window = this->manager->xkb.platform->base.space->stacking.active;
                             if (!window) {
                                 return;
                             }

                             // Ignore some special types.
                             if (win::is_desktop(window) || win::is_dock(window)) {
                                 return;
                             }

                             this->set_layout(get_layout(layouts, window));
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
        auto window = this->manager->xkb.platform->base.space->stacking.active;
        if (!window) {
            return;
        }

        // Ignore some special types.
        if (win::is_desktop(window) || win::is_dock(window)) {
            return;
        }

        auto it = layouts.find(window);

        if (it == layouts.end()) {
            layouts.insert({window, index});
            QObject::connect(window->qobject.get(),
                             &window_t::qobject_t::closed,
                             this->qobject.get(),
                             [this, window] { layouts.erase(window); });
        } else {
            it->second = index;
        }
    }

private:
    std::unordered_map<window_t*, uint32_t> layouts;
};

template<typename Manager>
class application_layout_policy : public layout_policy<Manager>
{
public:
    using window_t = typename layout_policy<Manager>::window_t;

    application_layout_policy(Manager* manager, KConfigGroup const& config)
        : layout_policy<Manager>(manager, config)
    {
        auto& space = *manager->xkb.platform->base.space;
        QObject::connect(space.qobject.get(),
                         &win::space_qobject::clientActivated,
                         this->qobject.get(),
                         [this, &space] { handle_client_activated(space.stacking.active); });

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
                    if (auto const name = win->control->desktop_file_name; !name.isEmpty()) {
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
        auto window = this->manager->xkb.platform->base.space->stacking.active;
        if (!window) {
            return;
        }

        // Ignore some special types.
        if (win::is_desktop(window) || win::is_dock(window)) {
            return;
        }

        auto it = layouts.find(window);

        if (it == layouts.end()) {
            layouts.insert({window, index});
            QObject::connect(window->qobject.get(),
                             &window_t::qobject_t::closed,
                             this->qobject.get(),
                             [this, window] { layouts.erase(window); });
        } else {
            if (it->second == index) {
                return;
            }
            it->second = index;
        }

        // Update all layouts for the application.
        for (auto& [win, layout] : layouts) {
            if (win::belong_to_same_client(win, window)) {
                layout = index;
            }
        }
    }

private:
    void handle_client_activated(window_t* window)
    {
        if (!window) {
            return;
        }

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
            if (win::belong_to_same_client(window, win)) {
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

    std::unordered_map<window_t*, uint32_t> layouts;
    std::unordered_map<QByteArray, uint32_t> restored_layouts;
};

template<typename Manager>
std::unique_ptr<layout_policy<Manager>>
create_layout_policy(Manager* manager, KConfigGroup const& config, QString const& policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return std::make_unique<virtual_desktop_layout_policy<Manager>>(manager, config);
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
