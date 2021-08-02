/*
 * Copyright 2018 Marco Martin <mart@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "tabletmodemanager.h"

#include "input/redirect.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "main.h"

#include "input/platform.h"
#include "input/switch.h"
#include "input/touch.h"

#include <QTimer>
#include <QDBusConnection>

using namespace KWin;

KWIN_SINGLETON_FACTORY_VARIABLE(TabletModeManager, s_manager)

class TabletModeSwitchEventSpy : public QObject, public input::event_spy
{
public:
    explicit TabletModeSwitchEventSpy(TabletModeManager *parent)
        : QObject(parent)
        , m_parent(parent)
    {
    }

    void switchEvent(input::SwitchEvent *event) override
    {
        if (auto& ctrl = event->device()->control; !ctrl || !ctrl->is_tablet_mode_switch()) {
            return;
        }

        switch (event->state()) {
        case input::SwitchEvent::State::Off:
            m_parent->setIsTablet(false);
            break;
        case input::SwitchEvent::State::On:
            m_parent->setIsTablet(true);
            break;
        default:
            Q_UNREACHABLE();
        }
    }

private:
    TabletModeManager * const m_parent;
};


class TabletModeTouchpadRemovedSpy : public QObject
{
public:
    explicit TabletModeTouchpadRemovedSpy(TabletModeManager *parent)
        : QObject(parent)
        , m_parent(parent)
    {
        auto& plat = kwinApp()->input_redirect->platform;
        connect(plat, &input::platform::pointer_added, this, &TabletModeTouchpadRemovedSpy::check);
        connect(plat, &input::platform::pointer_removed, this, &TabletModeTouchpadRemovedSpy::check);
        connect(plat, &input::platform::touch_added, this, &TabletModeTouchpadRemovedSpy::check);
        connect(plat, &input::platform::touch_removed, this, &TabletModeTouchpadRemovedSpy::check);
        check();
    }

    void check() {
        if (!kwinApp()->input_redirect->platform) {
            return;
        }
        auto has_touch = !kwinApp()->input_redirect->platform->touchs.empty();
        auto has_pointer = !kwinApp()->input_redirect->platform->pointers.empty();
        m_parent->setTabletModeAvailable(has_touch);
        m_parent->setIsTablet(has_touch && !has_pointer);
    }

private:
    TabletModeManager * const m_parent;
};

TabletModeManager::TabletModeManager(QObject *parent)
    : QObject(parent)
{
    if (kwinApp()->input_redirect->hasTabletModeSwitch()) {
        kwinApp()->input_redirect->installInputEventSpy(new TabletModeSwitchEventSpy(this));
    } else {
        hasTabletModeInputChanged(false);
    }

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin"),
                                                 QStringLiteral("org.kde.KWin.TabletModeManager"),
                                                 this,
                                                 QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals
    );

    connect(kwinApp()->input_redirect.get(), &input::redirect::hasTabletModeSwitchChanged, this, &TabletModeManager::hasTabletModeInputChanged);
}

void KWin::TabletModeManager::hasTabletModeInputChanged(bool set)
{
    if (set) {
        kwinApp()->input_redirect->installInputEventSpy(new TabletModeSwitchEventSpy(this));
        setTabletModeAvailable(true);
    } else {
        auto setupDetector = [this] {
            auto spy = new TabletModeTouchpadRemovedSpy(this);
            connect(kwinApp()->input_redirect.get(), &input::redirect::hasTabletModeSwitchChanged, spy, [spy](bool set){
                if (set)
                    spy->deleteLater();
            });
        };
        setupDetector();
    }
}

bool TabletModeManager::isTabletModeAvailable() const
{
    return m_detecting;
}

bool TabletModeManager::isTablet() const
{
    return m_isTabletMode;
}

void TabletModeManager::setIsTablet(bool tablet)
{
    if (m_isTabletMode == tablet) {
        return;
    }

    m_isTabletMode = tablet;
    emit tabletModeChanged(tablet);
}

void KWin::TabletModeManager::setTabletModeAvailable(bool detecting)
{
    if (m_detecting != detecting) {
        m_detecting = detecting;
        emit tabletModeAvailableChanged(detecting);
    }
}
