/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_mode_manager.h"

#include "input/platform.h"
#include "input/pointer.h"
#include "input/spies/tablet_mode_switch.h"
#include "input/touch.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "main.h"

#include <QDBusConnection>

namespace KWin::input::dbus
{

// TODO(romangg): Is this like a regular event spy or a different kind of spy?
class tablet_mode_touchpad_removed_spy : public QObject
{
public:
    explicit tablet_mode_touchpad_removed_spy(tablet_mode_manager* parent)
        : QObject(parent)
        , m_parent(parent)
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
        m_parent->setTabletModeAvailable(has_touch);
        m_parent->setIsTablet(has_touch && !has_pointer);
    }

private:
    tablet_mode_manager* const m_parent;
};

tablet_mode_manager::tablet_mode_manager()
{
    auto redirect = static_cast<input::wayland::redirect*>(kwinApp()->input->redirect);

    if (redirect->has_tablet_mode_switch()) {
        redirect->installInputEventSpy(new tablet_mode_switch_spy(this));
    } else {
        Q_EMIT redirect->has_tablet_mode_switch_changed(false);
    }

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin"),
                                                 QStringLiteral("org.kde.KWin.TabletModeManager"),
                                                 this,
                                                 QDBusConnection::ExportAllProperties
                                                     | QDBusConnection::ExportAllSignals);

    QObject::connect(redirect,
                     &input::wayland::redirect::has_tablet_mode_switch_changed,
                     this,
                     &tablet_mode_manager::hasTabletModeInputChanged);
}

tablet_mode_manager::~tablet_mode_manager()
{
    kwinApp()->input->redirect->uninstallInputEventSpy(spy);
}
void tablet_mode_manager::hasTabletModeInputChanged(bool set)
{
    if (set) {
        if (!spy) {
            spy = new tablet_mode_switch_spy(this);
            kwinApp()->input->redirect->installInputEventSpy(spy);
        }
        setTabletModeAvailable(true);
    } else {
        auto setupDetector = [this] {
            auto spy = new tablet_mode_touchpad_removed_spy(this);
            QObject::connect(static_cast<input::wayland::redirect*>(kwinApp()->input->redirect),
                             &input::wayland::redirect::has_tablet_mode_switch_changed,
                             spy,
                             [spy](bool set) {
                                 if (set)
                                     spy->deleteLater();
                             });
        };
        setupDetector();
    }
}

bool tablet_mode_manager::isTabletModeAvailable() const
{
    return m_detecting;
}

bool tablet_mode_manager::isTablet() const
{
    return m_isTabletMode;
}

void tablet_mode_manager::setIsTablet(bool tablet)
{
    if (m_isTabletMode == tablet) {
        return;
    }

    m_isTabletMode = tablet;
    Q_EMIT tabletModeChanged(tablet);
}

void tablet_mode_manager::setTabletModeAvailable(bool detecting)
{
    if (m_detecting != detecting) {
        m_detecting = detecting;
        Q_EMIT tabletModeAvailableChanged(detecting);
    }
}

}
