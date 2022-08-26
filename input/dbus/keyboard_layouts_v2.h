/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <KConfigGroup>
#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QVector>
#include <unordered_map>

class QAction;
class QDBusArgument;

namespace KWin::input
{

class keyboard;
class platform;

namespace dbus
{

struct keyboard_v2 {
    uint id{0};

    QString name;
    QString sys_name;
    uint vendor_id{0};
    uint product_id{0};
};

struct keyboard_v2_internal {
    keyboard_v2 data;
    input::keyboard* internal;
};

struct layout_names_v2 {
    QString short_name;
    QString long_name;
};

class keyboard_layouts_v2 : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayoutsV2")

public:
    keyboard_layouts_v2(input::platform* platform);

public Q_SLOTS:
    void switchToNextLayout(uint keyboard);
    void switchToPreviousLayout(uint keyboard);
    bool setLayout(uint keyboard, uint layout);
    uint getLayout(uint keyboard) const;
    QVector<layout_names_v2> getLayoutsList(uint keyboard) const;
    QVector<keyboard_v2> getKeyboards() const;

Q_SIGNALS:
    void keyboardAdded(keyboard_v2);
    void keyboardRemoved(uint);
    void layoutChanged(uint, uint);
    void layoutListChanged(uint);

private:
    void handle_keyboard_added(input::keyboard* keyboard);
    void handle_keyboard_removed(input::keyboard* keyboard);

    keyboard_v2_internal const* get_internal_keyboard(uint keyboard) const;

    std::unordered_map<uint, keyboard_v2_internal> keyboards;
};

KWIN_EXPORT QDBusArgument& operator<<(QDBusArgument& argument, keyboard_v2 const& keyboard);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument, keyboard_v2& keyboard);
KWIN_EXPORT QDBusArgument& operator<<(QDBusArgument& argument, layout_names_v2 const& names);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument, layout_names_v2& names);

}
}

Q_DECLARE_METATYPE(KWin::input::dbus::keyboard_v2)
Q_DECLARE_METATYPE(KWin::input::dbus::layout_names_v2)
