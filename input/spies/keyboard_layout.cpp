/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_layout.h"

#include "input/dbus/keyboard_layout.h"
#include "input/event.h"
#include "input/keyboard_layout_helpers.h"
#include "input/keyboard_layout_switching.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "platform.h"

#include <KGlobalAccel>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>

namespace KWin::input
{

keyboard_layout_spy::keyboard_layout_spy(xkb::manager& xkb, KSharedConfigPtr const& config)
    : xkb{xkb}
    , m_configGroup(config->group("Layout"))
{
}

void keyboard_layout_spy::init()
{
    QAction* switchKeyboardAction = new QAction(this);
    switchKeyboardAction->setObjectName(QStringLiteral("Switch to Next Keyboard Layout"));
    switchKeyboardAction->setProperty("componentName",
                                      QStringLiteral("KDE Keyboard Layout Switcher"));
    const QKeySequence sequence = QKeySequence(Qt::ALT + Qt::CTRL + Qt::Key_K);
    KGlobalAccel::self()->setDefaultShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    KGlobalAccel::self()->setShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    kwinApp()->platform->setupActionForGlobalAccel(switchKeyboardAction);
    QObject::connect(
        switchKeyboardAction, &QAction::triggered, this, &keyboard_layout_spy::switchToNextLayout);

    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/Layouts"),
                                          QStringLiteral("org.kde.keyboard"),
                                          QStringLiteral("reloadConfig"),
                                          this,
                                          SLOT(reconfigure()));

    reconfigure();
}

void keyboard_layout_spy::initDBusInterface()
{
    auto xkb = xkb::get_primary_xkb_keyboard();

    if (xkb->layouts_count() <= 1) {
        if (m_dbusInterface) {
            m_dbusInterface->deleteLater();
            m_dbusInterface = nullptr;
        }
        return;
    }

    if (m_dbusInterface) {
        return;
    }

    m_dbusInterface = new dbus::keyboard_layout(m_configGroup, this);

    QObject::connect(this,
                     &keyboard_layout_spy::layoutChanged,
                     m_dbusInterface,
                     &dbus::keyboard_layout::layoutChanged);
    // TODO: the signal might be emitted even if the list didn't change
    QObject::connect(this,
                     &keyboard_layout_spy::layoutsReconfigured,
                     m_dbusInterface,
                     &dbus::keyboard_layout::layoutListChanged);
}

void keyboard_layout_spy::switchToNextLayout()
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_next_layout();
    check_layout_change(xkb, previousLayout);
}

void keyboard_layout_spy::switchToPreviousLayout()
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_previous_layout();
    check_layout_change(xkb, previousLayout);
}

void keyboard_layout_spy::switchToLayout(xkb_layout_index_t index)
{
    auto xkb = xkb::get_primary_xkb_keyboard();
    auto const previousLayout = xkb->layout;
    xkb->switch_to_layout(index);
    check_layout_change(xkb, previousLayout);
}

void keyboard_layout_spy::reconfigure()
{
    if (m_configGroup.isValid()) {
        m_configGroup.config()->reparseConfiguration();
        const QString policyKey = m_configGroup.readEntry("SwitchMode", QStringLiteral("Global"));
        xkb.reconfigure();
        if (!m_policy || m_policy->name() != policyKey) {
            delete m_policy;
            m_policy = keyboard_layout_switching::policy::create(this, m_configGroup, policyKey);
        }
    } else {
        xkb.reconfigure();
    }
    resetLayout();
}

void keyboard_layout_spy::resetLayout()
{
    auto xkb = xkb::get_primary_xkb_keyboard();

    m_layout = xkb->layout;
    loadShortcuts();

    initDBusInterface();
    Q_EMIT layoutsReconfigured();
}

void keyboard_layout_spy::loadShortcuts()
{
    qDeleteAll(m_layoutShortcuts);
    m_layoutShortcuts.clear();

    auto xkb = xkb::get_primary_xkb_keyboard();

    const QString componentName = QStringLiteral("KDE Keyboard Layout Switcher");
    auto const count = xkb->layouts_count();

    for (uint i = 0; i < count; ++i) {
        // layout name is translated in the action name in keyboard kcm!
        const QString action = QStringLiteral("Switch keyboard layout to %1")
                                   .arg(translated_keyboard_layout(xkb->layout_name_from_index(i)));
        const auto shortcuts = KGlobalAccel::self()->globalShortcut(componentName, action);
        if (shortcuts.isEmpty()) {
            continue;
        }
        QAction* a = new QAction(this);
        a->setObjectName(action);
        a->setProperty("componentName", componentName);
        QObject::connect(
            a, &QAction::triggered, this, std::bind(&keyboard_layout_spy::switchToLayout, this, i));
        KGlobalAccel::self()->setShortcut(a, shortcuts, KGlobalAccel::Autoloading);
        m_layoutShortcuts << a;
    }
}

void keyboard_layout_spy::check_layout_change(input::xkb::keyboard* xkb, uint32_t old_layout)
{
    // Get here on key event or DBus call.
    // m_layout - layout saved last time OSD occurred
    // previousLayout - actual layout just before potential layout change
    // We need OSD if current layout deviates from any of these

    if (xkb != xkb::get_primary_xkb_keyboard()) {
        // We currently only inform about changes on the primary device.
        return;
    }

    if (m_layout != xkb->layout || old_layout != xkb->layout) {
        m_layout = xkb->layout;
        notifyLayoutChange();
        Q_EMIT layoutChanged(xkb->layout);
    }
}

void keyboard_layout_spy::notifyLayoutChange()
{
    auto xkb = xkb::get_primary_xkb_keyboard();

    // notify OSD service about the new layout
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                      QStringLiteral("/org/kde/osdService"),
                                                      QStringLiteral("org.kde.osdService"),
                                                      QStringLiteral("kbdLayoutChanged"));

    msg << translated_keyboard_layout(xkb->layout_name());

    QDBusConnection::sessionBus().asyncCall(msg);
}

}
