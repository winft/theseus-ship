/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QObject>
#include <QVector>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QString>
#include <string>

typedef uint32_t xkb_layout_index_t;

class QAction;
class QDBusArgument;

namespace KWin::input
{

namespace dbus
{
class keyboard_layout;
}

namespace keyboard_layout_switching
{
class policy;
}

namespace xkb
{
class keyboard;
class manager;

inline QString translated_keyboard_layout(std::string const& layout)
{
    return i18nd("xkeyboard-config", layout.c_str());
}

class KWIN_EXPORT layout_manager : public QObject
{
    Q_OBJECT
public:
    layout_manager(xkb::manager& xkb, KSharedConfigPtr const& config);

    void init();

    void check_layout_change(xkb::keyboard* xkb, uint32_t old_layout);
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

    xkb::manager& xkb;
    xkb_layout_index_t m_layout = 0;
    KConfigGroup m_configGroup;
    QVector<QAction*> m_layoutShortcuts;
    dbus::keyboard_layout* m_dbusInterface = nullptr;
    keyboard_layout_switching::policy* m_policy = nullptr;
};

}
}
