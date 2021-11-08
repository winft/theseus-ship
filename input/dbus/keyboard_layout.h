/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QVector>

#include <KConfigGroup>
#include <KSharedConfig>

class QAction;
class QDBusArgument;

namespace KWin::input
{
class keyboard_layout_spy;

namespace dbus
{

class keyboard_layout : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayouts")

public:
    keyboard_layout(KConfigGroup const& configGroup, input::keyboard_layout_spy* parent);
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

private:
    KConfigGroup const& m_configGroup;
    input::keyboard_layout_spy* m_keyboardLayout;
};

QDBusArgument& operator<<(QDBusArgument& argument, const keyboard_layout::LayoutNames& layoutNames);
const QDBusArgument& operator>>(const QDBusArgument& argument,
                                keyboard_layout::LayoutNames& layoutNames);

}
}

Q_DECLARE_METATYPE(KWin::input::dbus::keyboard_layout::LayoutNames)
