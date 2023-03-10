/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform_qobject.h"
#include "kwin_export.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>
#include <unordered_map>

class QAction;
class QDBusArgument;

namespace KWin::input
{

class keyboard;

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

class KWIN_EXPORT keyboard_layouts_v2 : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayoutsV2")

public:
    template<typename Platform>
    static std::unique_ptr<keyboard_layouts_v2> create(Platform* platform)
    {
        auto layouts = std::make_unique<keyboard_layouts_v2>();

        QObject::connect(platform->qobject.get(),
                         &input::platform_qobject::keyboard_added,
                         layouts.get(),
                         &keyboard_layouts_v2::handle_keyboard_added);
        QObject::connect(platform->qobject.get(),
                         &input::platform_qobject::keyboard_removed,
                         layouts.get(),
                         &keyboard_layouts_v2::handle_keyboard_removed);

        for (auto& keyboard : platform->keyboards) {
            layouts->handle_keyboard_added(keyboard);
        }

        return layouts;
    }

    keyboard_layouts_v2();

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
    uint keyboard_index{0};
};

KWIN_EXPORT QDBusArgument& operator<<(QDBusArgument& argument, keyboard_v2 const& keyboard);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument, keyboard_v2& keyboard);
KWIN_EXPORT QDBusArgument& operator<<(QDBusArgument& argument, layout_names_v2 const& names);
KWIN_EXPORT QDBusArgument const& operator>>(QDBusArgument const& argument, layout_names_v2& names);

}
}

Q_DECLARE_METATYPE(KWin::input::dbus::keyboard_v2)
Q_DECLARE_METATYPE(KWin::input::dbus::layout_names_v2)
