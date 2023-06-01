/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QObject>
#include <QVector>
#include <functional>

class QAction;
class QDBusArgument;

namespace KWin::input
{

namespace xkb
{
class keyboard;
}

namespace dbus
{

class KWIN_EXPORT keyboard_layout : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayouts")

public:
    keyboard_layout(KConfigGroup const& configGroup, std::function<xkb::keyboard*()> xkb_getter);
    ~keyboard_layout() override;

    struct LayoutNames {
        QString shortName;
        QString displayName;
        QString longName;
    };

public Q_SLOTS:
    void switchToNextLayout();
    void switchToPreviousLayout();
    bool setLayout(uint index);
    uint getLayout() const;
    QVector<LayoutNames> getLayoutsList() const;

Q_SIGNALS:
    void layoutChanged(uint index);
    void layoutListChanged();
    void next_layout_requested();
    void previous_layout_requested();

private:
    KConfigGroup const& m_configGroup;
    std::function<xkb::keyboard*()> xkb_getter;
};

QDBusArgument& operator<<(QDBusArgument& argument, const keyboard_layout::LayoutNames& layoutNames);
const QDBusArgument& operator>>(const QDBusArgument& argument,
                                keyboard_layout::LayoutNames& layoutNames);

}
}

Q_DECLARE_METATYPE(KWin::input::dbus::keyboard_layout::LayoutNames)
