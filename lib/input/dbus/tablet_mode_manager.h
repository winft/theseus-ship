/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"

#include "input/platform_qobject.h"
#include "input/redirect_qobject.h"
#include "input/spies/tablet_mode_switch.h"

#include <QDBusConnection>
#include <QObject>
#include <memory>

namespace KWin::input::dbus
{

// TODO(romangg): Is this like a regular event spy or a different kind of spy?
template<typename Manager>
class tablet_mode_touchpad_removed_spy : public QObject
{
public:
    explicit tablet_mode_touchpad_removed_spy(Manager& manager)
        : manager(manager)
    {
        auto q_platform = manager.redirect.platform.qobject.get();
        QObject::connect(q_platform,
                         &input::platform_qobject::pointer_added,
                         this,
                         &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(q_platform,
                         &input::platform_qobject::pointer_removed,
                         this,
                         &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(q_platform,
                         &input::platform_qobject::touch_added,
                         this,
                         &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(q_platform,
                         &input::platform_qobject::touch_removed,
                         this,
                         &tablet_mode_touchpad_removed_spy::check);
        check();
    }

    void check()
    {
        auto has_touch = !manager.redirect.platform.touchs.empty();
        auto has_pointer = !manager.redirect.platform.pointers.empty();
        manager.qobject->setTabletModeAvailable(has_touch);
        manager.qobject->setIsTablet(has_touch && !has_pointer);
    }

private:
    Manager& manager;
};

class KWIN_EXPORT tablet_mode_manager_qobject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.TabletModeManager")

    // Assuming such a switch is not pluggable for now.
    Q_PROPERTY(
        bool tabletModeAvailable READ isTabletModeAvailable NOTIFY tabletModeAvailableChanged)
    Q_PROPERTY(bool tabletMode READ isTablet NOTIFY tabletModeChanged)

public:
    bool isTabletModeAvailable() const;
    void setTabletModeAvailable(bool detecting);

    bool isTablet() const;
    void setIsTablet(bool tablet);

Q_SIGNALS:
    void tabletModeAvailableChanged(bool available);
    void tabletModeChanged(bool tabletMode);

private:
    bool m_tabletModeAvailable{false};
    bool m_isTabletMode{false};
    bool m_detecting{false};
};

template<typename Redirect>
class tablet_mode_manager
{
public:
    tablet_mode_manager(Redirect& redirect)
        : qobject{std::make_unique<tablet_mode_manager_qobject>()}
        , redirect{redirect}
    {
        if (redirect.has_tablet_mode_switch()) {
            redirect.m_spies.push_back(new tablet_mode_switch_spy(redirect, *qobject));
        } else {
            Q_EMIT redirect.qobject->has_tablet_mode_switch_changed(false);
        }

        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/org/kde/KWin"),
            QStringLiteral("org.kde.KWin.TabletModeManager"),
            qobject.get(),
            // NOTE: slots must be exported for properties to work correctly
            QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals
                | QDBusConnection::ExportAllSlots);

        QObject::connect(redirect.qobject.get(),
                         &input::redirect_qobject::has_tablet_mode_switch_changed,
                         qobject.get(),
                         [this](auto set) { hasTabletModeInputChanged(set); });
    }

    ~tablet_mode_manager()
    {
        remove_all(redirect.m_spies, spy);
    }

    std::unique_ptr<tablet_mode_manager_qobject> qobject;
    Redirect& redirect;

private:
    using removed_spy_t = tablet_mode_touchpad_removed_spy<tablet_mode_manager>;
    using mode_switch_spy_t = tablet_mode_switch_spy<Redirect, tablet_mode_manager_qobject>;

    void hasTabletModeInputChanged(bool set)
    {
        if (set) {
            if (!spy) {
                spy = new mode_switch_spy_t(redirect, *qobject);
                redirect.m_spies.push_back(spy);
            }
            qobject->setTabletModeAvailable(true);
        } else {
            auto setupDetector = [this] {
                removed_spy = std::make_unique<removed_spy_t>(*this);
                QObject::connect(redirect.qobject.get(),
                                 &input::redirect_qobject::has_tablet_mode_switch_changed,
                                 removed_spy.get(),
                                 [this](bool set) {
                                     if (set)
                                         removed_spy.reset();
                                 });
            };
            setupDetector();
        }
    }

    mode_switch_spy_t* spy{nullptr};
    std::unique_ptr<removed_spy_t> removed_spy;
};

}
