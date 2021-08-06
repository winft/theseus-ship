/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_layout.h"

#include "input/event.h"
#include "input/keyboard_layout_helpers.h"
#include "input/keyboard_layout_switching.h"
#include "input/spies/keyboard_layout.h"
#include "input/xkb.h"
#include "main.h"
#include "platform.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>

namespace KWin::input::dbus
{

static const QString s_keyboardService = QStringLiteral("org.kde.keyboard");
static const QString s_keyboardObject = QStringLiteral("/Layouts");

keyboard_layout::keyboard_layout(xkb* xkb,
                                 const KConfigGroup& configGroup,
                                 input::keyboard_layout_spy* parent)
    : QObject(parent)
    , m_xkb(xkb)
    , m_configGroup(configGroup)
    , m_keyboardLayout(parent)
{
    qRegisterMetaType<QVector<LayoutNames>>("QVector<LayoutNames>");
    qDBusRegisterMetaType<LayoutNames>();
    qDBusRegisterMetaType<QVector<LayoutNames>>();

    QDBusConnection::sessionBus().registerObject(s_keyboardObject,
                                                 this,
                                                 QDBusConnection::ExportAllSlots
                                                     | QDBusConnection::ExportAllSignals);
    QDBusConnection::sessionBus().registerService(s_keyboardService);
}

keyboard_layout::~keyboard_layout()
{
    QDBusConnection::sessionBus().unregisterService(s_keyboardService);
}

void keyboard_layout::switchToNextLayout()
{
    m_keyboardLayout->switchToNextLayout();
}

void keyboard_layout::switchToPreviousLayout()
{
    m_keyboardLayout->switchToPreviousLayout();
}

bool keyboard_layout::setLayout(uint index)
{
    const quint32 previousLayout = m_xkb->currentLayout();
    if (!m_xkb->switchToLayout(index)) {
        return false;
    }
    m_keyboardLayout->checkLayoutChange(previousLayout);
    return true;
}

uint keyboard_layout::getLayout() const
{
    return m_xkb->currentLayout();
}

QVector<keyboard_layout::LayoutNames> keyboard_layout::getLayoutsList() const
{
    // TODO: - should be handled by layout applet itself, it has nothing to do with KWin
    const QStringList displayNames = m_configGroup.readEntry("DisplayNames", QStringList());

    QVector<LayoutNames> ret;
    const int layoutsSize = m_xkb->numberOfLayouts();
    const int displayNamesSize = displayNames.size();
    for (int i = 0; i < layoutsSize; ++i) {
        ret.append({m_xkb->layoutShortName(i),
                    i < displayNamesSize ? displayNames.at(i) : QString(),
                    translated_keyboard_layout(m_xkb->layoutName(i))});
    }
    return ret;
}

QDBusArgument& operator<<(QDBusArgument& argument, const keyboard_layout::LayoutNames& layoutNames)
{
    argument.beginStructure();
    argument << layoutNames.shortName << layoutNames.displayName << layoutNames.longName;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument,
                                keyboard_layout::LayoutNames& layoutNames)
{
    argument.beginStructure();
    argument >> layoutNames.shortName >> layoutNames.displayName >> layoutNames.longName;
    argument.endStructure();
    return argument;
}

}
