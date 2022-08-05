/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QObject>
#include <QString>
#include <QVector>
#include <string>

typedef uint32_t xkb_layout_index_t;

class QAction;
class QDBusArgument;

namespace KWin::input
{
class keyboard;

namespace dbus
{
class keyboard_layout;
class keyboard_layouts_v2;
}

namespace xkb
{
class keyboard;
class layout_policy;
class manager;

inline QString translated_keyboard_layout(std::string const& layout)
{
    return i18nd("xkeyboard-config", layout.c_str());
}

class KWIN_EXPORT layout_manager_qobject : public QObject
{
    Q_OBJECT
public:
    layout_manager_qobject(std::function<void()> reconfigure_callback);

Q_SIGNALS:
    void layoutChanged(uint index);
    void layoutsReconfigured();

private Q_SLOTS:
    void reconfigure();

private:
    std::function<void()> reconfigure_callback;
};

class KWIN_EXPORT layout_manager
{
public:
    layout_manager(xkb::manager& xkb, KSharedConfigPtr const& config);

    void switchToNextLayout();
    void switchToPreviousLayout();

    std::unique_ptr<layout_manager_qobject> qobject;
    xkb::manager& xkb;

private:
    void reconfigure();
    void initDBusInterface();
    void init_dbus_interface_v2();
    void add_keyboard(input::keyboard* keyboard);
    void handle_layout_change(xkb::keyboard* xkb);
    void send_layout_to_osd(xkb::keyboard* xkb);
    void switchToLayout(xkb_layout_index_t index);
    void load_shortcuts(xkb::keyboard* xkb);

    KConfigGroup m_configGroup;
    QVector<QAction*> m_layoutShortcuts;
    dbus::keyboard_layout* m_dbusInterface = nullptr;
    dbus::keyboard_layouts_v2* dbus_interface_v2{nullptr};
    xkb::layout_policy* m_policy = nullptr;
};

}
}
