/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_mode_manager.h"

#include "input/platform.h"
#include "input/pointer.h"
#include "input/redirect.h"
#include "input/spies/tablet_mode_switch.h"
#include "input/touch.h"

#include "main.h"

#include <QDBusConnection>

namespace KWin::input::dbus
{

KWIN_SINGLETON_FACTORY_VARIABLE(tablet_mode_manager, s_manager)

// TODO(romangg): Is this like a regular event spy or a different kind of spy?
class tablet_mode_touchpad_removed_spy : public QObject
{
public:
    explicit tablet_mode_touchpad_removed_spy(tablet_mode_manager* parent)
        : QObject(parent)
        , m_parent(parent)
    {
        auto& plat = kwinApp()->input_redirect->platform;
        connect(
            plat, &input::platform::pointer_added, this, &tablet_mode_touchpad_removed_spy::check);
        connect(plat,
                &input::platform::pointer_removed,
                this,
                &tablet_mode_touchpad_removed_spy::check);
        connect(
            plat, &input::platform::touch_added, this, &tablet_mode_touchpad_removed_spy::check);
        connect(
            plat, &input::platform::touch_removed, this, &tablet_mode_touchpad_removed_spy::check);
        check();
    }

    void check()
    {
        if (!kwinApp()->input_redirect->platform) {
            return;
        }
        auto has_touch = !kwinApp()->input_redirect->platform->touchs.empty();
        auto has_pointer = !kwinApp()->input_redirect->platform->pointers.empty();
        m_parent->setTabletModeAvailable(has_touch);
        m_parent->setIsTablet(has_touch && !has_pointer);
    }

private:
    tablet_mode_manager* const m_parent;
};

tablet_mode_manager::tablet_mode_manager(QObject* parent)
    : QObject(parent)
{
    if (kwinApp()->input_redirect->hasTabletModeSwitch()) {
        kwinApp()->input_redirect->installInputEventSpy(new tablet_mode_switch_spy(this));
    } else {
        hasTabletModeInputChanged(false);
    }

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin"),
                                                 QStringLiteral("org.kde.KWin.TabletModeManager"),
                                                 this,
                                                 QDBusConnection::ExportAllProperties
                                                     | QDBusConnection::ExportAllSignals);

    connect(kwinApp()->input_redirect.get(),
            &input::redirect::hasTabletModeSwitchChanged,
            this,
            &tablet_mode_manager::hasTabletModeInputChanged);
}

void tablet_mode_manager::hasTabletModeInputChanged(bool set)
{
    if (set) {
        kwinApp()->input_redirect->installInputEventSpy(new tablet_mode_switch_spy(this));
        setTabletModeAvailable(true);
    } else {
        auto setupDetector = [this] {
            auto spy = new tablet_mode_touchpad_removed_spy(this);
            connect(kwinApp()->input_redirect.get(),
                    &input::redirect::hasTabletModeSwitchChanged,
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
    emit tabletModeChanged(tablet);
}

void tablet_mode_manager::setTabletModeAvailable(bool detecting)
{
    if (m_detecting != detecting) {
        m_detecting = detecting;
        emit tabletModeAvailableChanged(detecting);
    }
}

}
