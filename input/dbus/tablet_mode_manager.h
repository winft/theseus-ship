/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"

#include "input/platform.h"
#include "input/pointer.h"
#include "input/spies/tablet_mode_switch.h"
#include "input/touch.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "main.h"

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
        auto plat = kwinApp()->input.get();
        QObject::connect(
            plat, &input::platform::pointer_added, this, &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(plat,
                         &input::platform::pointer_removed,
                         this,
                         &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(
            plat, &input::platform::touch_added, this, &tablet_mode_touchpad_removed_spy::check);
        QObject::connect(
            plat, &input::platform::touch_removed, this, &tablet_mode_touchpad_removed_spy::check);
        check();
    }

    void check()
    {
        auto& plat = kwinApp()->input;
        auto has_touch = !plat->touchs.empty();
        auto has_pointer = !plat->pointers.empty();
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

class tablet_mode_manager
{
public:
    tablet_mode_manager()
        : qobject{std::make_unique<tablet_mode_manager_qobject>()}
    {
        auto redirect = static_cast<input::wayland::redirect*>(kwinApp()->input->redirect);

        if (redirect->has_tablet_mode_switch()) {
            redirect->installInputEventSpy(
                new tablet_mode_switch_spy(*kwinApp()->input->redirect, *qobject));
        } else {
            Q_EMIT redirect->has_tablet_mode_switch_changed(false);
        }

        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/org/kde/KWin"),
            QStringLiteral("org.kde.KWin.TabletModeManager"),
            qobject.get(),
            QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);

        QObject::connect(redirect,
                         &input::wayland::redirect::has_tablet_mode_switch_changed,
                         qobject.get(),
                         [this](auto set) { hasTabletModeInputChanged(set); });
    }

    ~tablet_mode_manager()
    {
        kwinApp()->input->redirect->uninstallInputEventSpy(spy);
    }

    std::unique_ptr<tablet_mode_manager_qobject> qobject;

private:
    using removed_spy_t = tablet_mode_touchpad_removed_spy<tablet_mode_manager>;

    void hasTabletModeInputChanged(bool set)
    {
        if (set) {
            if (!spy) {
                spy = new tablet_mode_switch_spy(*kwinApp()->input->redirect, *qobject);
                kwinApp()->input->redirect->installInputEventSpy(spy);
            }
            qobject->setTabletModeAvailable(true);
        } else {
            auto setupDetector = [this] {
                removed_spy = std::make_unique<removed_spy_t>(*this);
                QObject::connect(static_cast<input::wayland::redirect*>(kwinApp()->input->redirect),
                                 &input::wayland::redirect::has_tablet_mode_switch_changed,
                                 removed_spy.get(),
                                 [this](bool set) {
                                     if (set)
                                         removed_spy.reset();
                                 });
            };
            setupDetector();
        }
    }

    tablet_mode_switch_spy<tablet_mode_manager_qobject>* spy{nullptr};
    std::unique_ptr<removed_spy_t> removed_spy;
};

}
