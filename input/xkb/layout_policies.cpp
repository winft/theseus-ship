/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "layout_policies.h"

#include "helpers.h"
#include "layout_manager.h"

#include "input/redirect.h"
#include "toplevel.h"
#include "win/control.h"
#include "win/net.h"
#include "win/space.h"
#include "win/util.h"
#include "win/virtual_desktops.h"

namespace KWin
{

namespace input::xkb
{

layout_policy::layout_policy(layout_manager* manager, KConfigGroup const& config)
    : QObject(manager)
    , manager(manager)
    , config(config)
{
    QObject::connect(
        manager, &layout_manager::layoutsReconfigured, this, &layout_policy::clear_cache);
    QObject::connect(
        manager, &layout_manager::layoutChanged, this, &layout_policy::handle_layout_change);
}

layout_policy::~layout_policy() = default;

void layout_policy::set_layout(uint index)
{
    xkb::get_primary_xkb_keyboard()->switch_to_layout(index);
}

layout_policy*
layout_policy::create(layout_manager* manager, KConfigGroup const& config, QString const& policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return new virtual_desktop_layout_policy(manager, config);
    }
    if (policy.toLower() == QStringLiteral("window")) {
        return new window_layout_policy(manager);
    }
    if (policy.toLower() == QStringLiteral("winclass")) {
        return new application_layout_policy(manager, config);
    }
    return new global_layout_policy(manager, config);
}

char const layout_policy::default_layout_entry_key_prefix[] = "LayoutDefault";

QString const layout_policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name() % QLatin1Char('_');
}

void layout_policy::clear_layouts()
{
    auto const layout_entries = config.keyList().filter(default_layout_entry_key_prefix);
    for (const auto& entry : layout_entries) {
        config.deleteEntry(entry);
    }
}

QString const global_layout_policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name();
}

global_layout_policy::global_layout_policy(layout_manager* manager, KConfigGroup const& config)
    : layout_policy(manager, config)
{
    auto session_manager = manager->xkb.platform->redirect->space.session_manager.get();
    QObject::connect(
        session_manager, &win::session_manager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();
            if (auto const layout = xkb::get_primary_xkb_keyboard()->layout) {
                this->config.writeEntry(default_layout_entry_key(), layout);
            }
        });

    QObject::connect(session_manager, &win::session_manager::loadSessionRequested, this, [this] {
        if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
            set_layout(this->config.readEntry(default_layout_entry_key(), 0));
        }
    });
}

virtual_desktop_layout_policy::virtual_desktop_layout_policy(layout_manager* manager,
                                                             KConfigGroup const& config)
    : layout_policy(manager, config)
{
    auto& space = manager->xkb.platform->redirect->space;
    QObject::connect(space.virtual_desktop_manager.get(),
                     &win::virtual_desktop_manager::currentChanged,
                     this,
                     &virtual_desktop_layout_policy::handle_desktop_change);

    auto session_manager = space.session_manager.get();
    QObject::connect(
        session_manager, &win::session_manager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();

            for (auto const& [vd, layout] : layouts) {
                if (!layout) {
                    continue;
                }

                this->config.writeEntry(
                    default_layout_entry_key()
                        % QLatin1String(QByteArray::number(vd->x11DesktopNumber())),
                    layout);
            }
        });

    QObject::connect(session_manager, &win::session_manager::loadSessionRequested, this, [this] {
        if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
            auto const& desktops
                = this->manager->xkb.platform->redirect->space.virtual_desktop_manager->desktops();

            for (auto const desktop : desktops) {
                uint const layout = this->config.readEntry(
                    default_layout_entry_key()
                        % QLatin1String(QByteArray::number(desktop->x11DesktopNumber())),
                    0u);

                if (layout) {
                    layouts.insert({desktop, layout});
                    QObject::connect(desktop,
                                     &win::virtual_desktop::aboutToBeDestroyed,
                                     this,
                                     [this, desktop] { layouts.erase(desktop); });
                }
            }

            handle_desktop_change();
        }
    });
}

void virtual_desktop_layout_policy::clear_cache()
{
    layouts.clear();
}

namespace
{
template<typename T, typename U>
uint32_t getLayout(T const& layouts, U const& reference)
{
    auto it = layouts.find(reference);
    if (it == layouts.end()) {
        return 0;
    }
    return it->second;
}
}

void virtual_desktop_layout_policy::handle_desktop_change()
{
    if (auto desktop
        = manager->xkb.platform->redirect->space.virtual_desktop_manager->currentDesktop()) {
        set_layout(getLayout(layouts, desktop));
    }
}

void virtual_desktop_layout_policy::handle_layout_change(uint index)
{
    auto desktop = manager->xkb.platform->redirect->space.virtual_desktop_manager->currentDesktop();
    if (!desktop) {
        return;
    }

    auto it = layouts.find(desktop);

    if (it == layouts.end()) {
        layouts.insert({desktop, index});
        QObject::connect(desktop, &win::virtual_desktop::aboutToBeDestroyed, this, [this, desktop] {
            layouts.erase(desktop);
        });
    } else {
        it->second = index;
    }
}

window_layout_policy::window_layout_policy(layout_manager* manager)
    : layout_policy(manager)
{
    QObject::connect(manager->xkb.platform->redirect->space.qobject.get(),
                     &win::space::qobject_t::clientActivated,
                     this,
                     [this](auto window) {
                         if (!window) {
                             return;
                         }

                         // Ignore some special types.
                         if (win::is_desktop(window) || win::is_dock(window)) {
                             return;
                         }

                         set_layout(getLayout(layouts, window));
                     });
}

void window_layout_policy::clear_cache()
{
    layouts.clear();
}

void window_layout_policy::handle_layout_change(uint index)
{
    auto window = manager->xkb.platform->redirect->space.activeClient();
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
        QObject::connect(
            window, &Toplevel::closed, this, [this, window] { layouts.erase(window); });
    } else {
        it->second = index;
    }
}

application_layout_policy::application_layout_policy(layout_manager* manager,
                                                     KConfigGroup const& config)
    : layout_policy(manager, config)
{
    auto& space = manager->xkb.platform->redirect->space;
    QObject::connect(space.qobject.get(),
                     &win::space::qobject_t::clientActivated,
                     this,
                     &application_layout_policy::handle_client_activated);

    auto session_manager = space.session_manager.get();
    QObject::connect(
        session_manager, &win::session_manager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();

            for (auto const& [win, layout] : layouts) {
                if (!layout) {
                    continue;
                }
                if (auto const name = win->control->desktop_file_name(); !name.isEmpty()) {
                    this->config.writeEntry(default_layout_entry_key() % QLatin1String(name),
                                            layout);
                }
            }
        });
    QObject::connect(session_manager, &win::session_manager::loadSessionRequested, this, [this] {
        if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
            auto const keyPrefix = default_layout_entry_key();
            auto const keyList = this->config.keyList().filter(keyPrefix);
            for (auto const& key : keyList) {
                restored_layouts.insert({QStringView(key).mid(keyPrefix.size()).toLatin1(),
                                         this->config.readEntry(key, 0)});
            }
        }
    });
}

void application_layout_policy::handle_client_activated(Toplevel* window)
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
        set_layout(it->second);
        return;
    }

    for (auto const& [win, layout] : layouts) {
        if (win::belong_to_same_client(window, win)) {
            set_layout(layout);
            handle_layout_change(layout);
            return;
        }
    }

    auto restored_layout = 0;

    if (auto restored_it = restored_layouts.find(window->control->desktop_file_name());
        restored_it != restored_layouts.end()) {
        restored_layout = restored_it->second;
        restored_layouts.erase(restored_it);
    }

    set_layout(restored_layout);

    if (auto index = xkb::get_primary_xkb_keyboard()->layout) {
        handle_layout_change(index);
    }
}

void application_layout_policy::clear_cache()
{
    layouts.clear();
}

void application_layout_policy::handle_layout_change(uint index)
{
    auto window = manager->xkb.platform->redirect->space.activeClient();
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
        QObject::connect(
            window, &Toplevel::closed, this, [this, window] { layouts.erase(window); });
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

}
}
