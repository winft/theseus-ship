/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_layout_switching.h"

#include "spies/keyboard_layout.h"
#include "toplevel.h"
#include "virtualdesktops.h"
#include "workspace.h"
#include "xkb/helpers.h"

#include "win/control.h"
#include "win/net.h"
#include "win/util.h"

namespace KWin
{

namespace input::keyboard_layout_switching
{

policy::policy(keyboard_layout_spy* layout, KConfigGroup const& config)
    : QObject(layout)
    , config(config)
    , layout(layout)
{
    QObject::connect(layout, &keyboard_layout_spy::layoutsReconfigured, this, &policy::clear_cache);
    QObject::connect(
        layout, &keyboard_layout_spy::layoutChanged, this, &policy::handle_layout_change);
}

policy::~policy() = default;

void policy::set_layout(uint index)
{
    auto xkb = xkb::get_primary_xkb_keyboard();

    auto const previous_layout = xkb->layout;
    xkb->switch_to_layout(index);

    if (previous_layout != xkb->layout) {
        Q_EMIT layout->layoutChanged(xkb->layout);
    }
}

policy*
policy::create(keyboard_layout_spy* layout, KConfigGroup const& config, QString const& policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return new virtual_desktop_policy(layout, config);
    }
    if (policy.toLower() == QStringLiteral("window")) {
        return new window_policy(layout);
    }
    if (policy.toLower() == QStringLiteral("winclass")) {
        return new application_policy(layout, config);
    }
    return new global_policy(layout, config);
}

char const policy::default_layout_entry_key_prefix[] = "LayoutDefault";

QString const policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name() % QLatin1Char('_');
}

void policy::clear_layouts()
{
    auto const layout_entries = config.keyList().filter(default_layout_entry_key_prefix);
    for (const auto& entry : layout_entries) {
        config.deleteEntry(entry);
    }
}

QString const global_policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name();
}

global_policy::global_policy(keyboard_layout_spy* layout, KConfigGroup const& config)
    : policy(layout, config)
{
    QObject::connect(
        workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();
            if (auto const layout = xkb::get_primary_xkb_keyboard()->layout) {
                this->config.writeEntry(default_layout_entry_key(), layout);
            }
        });

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this] {
            if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
                set_layout(this->config.readEntry(default_layout_entry_key(), 0));
            }
        });
}

virtual_desktop_policy::virtual_desktop_policy(keyboard_layout_spy* layout,
                                               KConfigGroup const& config)
    : policy(layout, config)
{
    QObject::connect(VirtualDesktopManager::self(),
                     &VirtualDesktopManager::currentChanged,
                     this,
                     &virtual_desktop_policy::handle_desktop_change);

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this, [this] {
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

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this] {
            if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
                auto const& desktops = VirtualDesktopManager::self()->desktops();

                for (KWin::VirtualDesktop* const desktop : desktops) {
                    uint const layout = this->config.readEntry(
                        default_layout_entry_key()
                            % QLatin1String(QByteArray::number(desktop->x11DesktopNumber())),
                        0u);

                    if (layout) {
                        layouts.insert({desktop, layout});
                        QObject::connect(desktop,
                                         &VirtualDesktop::aboutToBeDestroyed,
                                         this,
                                         [this, desktop] { layouts.erase(desktop); });
                    }
                }

                handle_desktop_change();
            }
        });
}

void virtual_desktop_policy::clear_cache()
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

void virtual_desktop_policy::handle_desktop_change()
{
    if (auto desktop = VirtualDesktopManager::self()->currentDesktop()) {
        set_layout(getLayout(layouts, desktop));
    }
}

void virtual_desktop_policy::handle_layout_change(uint index)
{
    auto desktop = VirtualDesktopManager::self()->currentDesktop();
    if (!desktop) {
        return;
    }

    auto it = layouts.find(desktop);

    if (it == layouts.end()) {
        layouts.insert({desktop, index});
        QObject::connect(desktop, &VirtualDesktop::aboutToBeDestroyed, this, [this, desktop] {
            layouts.erase(desktop);
        });
    } else {
        it->second = index;
    }
}

window_policy::window_policy(keyboard_layout_spy* layout)
    : policy(layout)
{
    QObject::connect(workspace(), &Workspace::clientActivated, this, [this](auto window) {
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

void window_policy::clear_cache()
{
    layouts.clear();
}

void window_policy::handle_layout_change(uint index)
{
    auto window = workspace()->activeClient();
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
            window, &Toplevel::windowClosed, this, [this, window] { layouts.erase(window); });
    } else {
        it->second = index;
    }
}

application_policy::application_policy(keyboard_layout_spy* layout, KConfigGroup const& config)
    : policy(layout, config)
{
    QObject::connect(workspace(),
                     &Workspace::clientActivated,
                     this,
                     &application_policy::handle_client_activated);

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this, [this] {
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

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this] {
            if (xkb::get_primary_xkb_keyboard()->layouts_count() > 1) {
                auto const keyPrefix = default_layout_entry_key();
                auto const keyList = this->config.keyList().filter(keyPrefix);
                for (auto const& key : keyList) {
                    restored_layouts.insert(
                        {key.midRef(keyPrefix.size()).toLatin1(), this->config.readEntry(key, 0)});
                }
            }
        });
}

void application_policy::handle_client_activated(Toplevel* window)
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

void application_policy::clear_cache()
{
    layouts.clear();
}

void application_policy::handle_layout_change(uint index)
{
    auto window = workspace()->activeClient();
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
            window, &Toplevel::windowClosed, this, [this, window] { layouts.erase(window); });
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
