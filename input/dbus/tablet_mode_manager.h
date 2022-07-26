/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"

#include <QObject>
#include <memory>

namespace KWin::input
{

class tablet_mode_switch_spy;

namespace dbus
{

class tablet_mode_touchpad_removed_spy;

class KWIN_EXPORT tablet_mode_manager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.TabletModeManager")

    // Assuming such a switch is not pluggable for now.
    Q_PROPERTY(
        bool tabletModeAvailable READ isTabletModeAvailable NOTIFY tabletModeAvailableChanged)
    Q_PROPERTY(bool tabletMode READ isTablet NOTIFY tabletModeChanged)

public:
    tablet_mode_manager();
    ~tablet_mode_manager() override;

    bool isTabletModeAvailable() const;
    void setTabletModeAvailable(bool detecting);

    bool isTablet() const;
    void setIsTablet(bool tablet);

Q_SIGNALS:
    void tabletModeAvailableChanged(bool available);
    void tabletModeChanged(bool tabletMode);

private:
    void hasTabletModeInputChanged(bool set);

    tablet_mode_switch_spy* spy{nullptr};
    std::unique_ptr<tablet_mode_touchpad_removed_spy> removed_spy;
    bool m_tabletModeAvailable{false};
    bool m_isTabletMode{false};
    bool m_detecting{false};
};

}
}
