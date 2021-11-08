/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

#include <QObject>
#include <QVector>

#include <KConfigGroup>
#include <KSharedConfig>

typedef uint32_t xkb_layout_index_t;

class QAction;
class QDBusArgument;

namespace KWin::input
{
class xkb;
class xkb_keyboard;

namespace dbus
{
class keyboard_layout;
}

namespace keyboard_layout_switching
{
class policy;
}

class KWIN_EXPORT keyboard_layout_spy : public QObject, public input::event_spy
{
    Q_OBJECT
public:
    keyboard_layout_spy(input::xkb& xkb, KSharedConfigPtr const& config);

    void init();

    void check_layout_change(input::xkb_keyboard* xkb, uint32_t old_layout);
    void switchToNextLayout();
    void switchToPreviousLayout();
    void resetLayout();

Q_SIGNALS:
    void layoutChanged(uint index);
    void layoutsReconfigured();

private Q_SLOTS:
    void reconfigure();

private:
    void initDBusInterface();
    void notifyLayoutChange();
    void switchToLayout(xkb_layout_index_t index);
    void loadShortcuts();

    input::xkb& xkb;
    xkb_layout_index_t m_layout = 0;
    KConfigGroup m_configGroup;
    QVector<QAction*> m_layoutShortcuts;
    dbus::keyboard_layout* m_dbusInterface = nullptr;
    keyboard_layout_switching::policy* m_policy = nullptr;
};

}
