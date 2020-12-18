/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016, 2017 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "keyboard_layout.h"
#include "keyboard_layout_switching.h"
#include "keyboard_input.h"
#include "input_event.h"
#include "main.h"
#include "platform.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

namespace KWin
{

KeyboardLayout::KeyboardLayout(Xkb *xkb)
    : QObject()
    , m_xkb(xkb)
{
}

KeyboardLayout::~KeyboardLayout() = default;

static QString translatedLayout(const QString &layout)
{
    return i18nd("xkeyboard-config", layout.toUtf8().constData());
}

void KeyboardLayout::init()
{
    QAction *switchKeyboardAction = new QAction(this);
    switchKeyboardAction->setObjectName(QStringLiteral("Switch to Next Keyboard Layout"));
    switchKeyboardAction->setProperty("componentName", QStringLiteral("KDE Keyboard Layout Switcher"));
    const QKeySequence sequence = QKeySequence(Qt::ALT+Qt::CTRL+Qt::Key_K);
    KGlobalAccel::self()->setDefaultShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    KGlobalAccel::self()->setShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
    kwinApp()->platform()->setupActionForGlobalAccel(switchKeyboardAction);
    connect(switchKeyboardAction, &QAction::triggered, this, &KeyboardLayout::switchToNextLayout);

    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/Layouts"),
                                          QStringLiteral("org.kde.keyboard"),
                                          QStringLiteral("reloadConfig"),
                                          this,
                                          SLOT(reconfigure()));

    reconfigure();
}

void KeyboardLayout::initDBusInterface()
{
    if (m_xkb->numberOfLayouts() <= 1) {
        if (m_dbusInterface) {
            m_dbusInterface->deleteLater();
            m_dbusInterface = nullptr;
        }
        return;
    }
    if (m_dbusInterface) {
        return;
    }
    m_dbusInterface = new KeyboardLayoutDBusInterface(m_xkb, this);
    connect(this, &KeyboardLayout::layoutChanged, m_dbusInterface,
        [this] {
            emit m_dbusInterface->layoutChanged(m_xkb->layoutName());
        }
    );
    // TODO: the signal might be emitted even if the list didn't change
    connect(this, &KeyboardLayout::layoutsReconfigured, m_dbusInterface, &KeyboardLayoutDBusInterface::layoutListChanged);
}

void KeyboardLayout::switchToNextLayout()
{
    const quint32 previousLayout = m_xkb->currentLayout();
    m_xkb->switchToNextLayout();
    checkLayoutChange(previousLayout);
}

void KeyboardLayout::switchToPreviousLayout()
{
    const quint32 previousLayout = m_xkb->currentLayout();
    m_xkb->switchToPreviousLayout();
    checkLayoutChange(previousLayout);
}

void KeyboardLayout::switchToLayout(xkb_layout_index_t index)
{
    const quint32 previousLayout = m_xkb->currentLayout();
    m_xkb->switchToLayout(index);
    checkLayoutChange(previousLayout);
}

void KeyboardLayout::reconfigure()
{
    if (m_config) {
        m_config->reparseConfiguration();
        const KConfigGroup layoutGroup = m_config->group("Layout");
        const QString policyKey = layoutGroup.readEntry("SwitchMode", QStringLiteral("Global"));
        m_xkb->reconfigure();
        if (!m_policy || m_policy->name() != policyKey) {
            delete m_policy;
            m_policy = KeyboardLayoutSwitching::Policy::create(m_xkb, this, layoutGroup, policyKey);
        }
    } else {
        m_xkb->reconfigure();
    }
    resetLayout();
}

void KeyboardLayout::resetLayout()
{
    m_layout = m_xkb->currentLayout();
    loadShortcuts();

    initDBusInterface();
    emit layoutsReconfigured();
}

void KeyboardLayout::loadShortcuts()
{
    qDeleteAll(m_layoutShortcuts);
    m_layoutShortcuts.clear();
    const auto layouts = m_xkb->layoutNames();
    const QString componentName = QStringLiteral("KDE Keyboard Layout Switcher");
    for (auto it = layouts.begin(); it != layouts.end(); it++) {
        // layout name is translated in the action name in keyboard kcm!
        const QString action = QStringLiteral("Switch keyboard layout to %1").arg(translatedLayout(it.value()));
        const auto shortcuts = KGlobalAccel::self()->globalShortcut(componentName, action);
        if (shortcuts.isEmpty()) {
            continue;
        }
        QAction *a = new QAction(this);
        a->setObjectName(action);
        a->setProperty("componentName", componentName);
        connect(a, &QAction::triggered, this,
                std::bind(&KeyboardLayout::switchToLayout, this, it.key()));
        KGlobalAccel::self()->setShortcut(a, shortcuts, KGlobalAccel::Autoloading);
        m_layoutShortcuts << a;
    }
}

void KeyboardLayout::checkLayoutChange(quint32 previousLayout)
{
    // Get here on key event or DBus call.
    // m_layout - layout saved last time OSD occurred
    // previousLayout - actual layout just before potential layout change
    // We need OSD if current layout deviates from any of these
    const auto layout = m_xkb->currentLayout();
    if (m_layout != layout || previousLayout != layout) {
        m_layout = layout;
        notifyLayoutChange();
        emit layoutChanged();
    }
}

void KeyboardLayout::notifyLayoutChange()
{
    // notify OSD service about the new layout
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("/org/kde/osdService"),
        QStringLiteral("org.kde.osdService"),
        QStringLiteral("kbdLayoutChanged"));

    msg << translatedLayout(m_xkb->layoutName());

    QDBusConnection::sessionBus().asyncCall(msg);
}

static const QString s_keyboardService = QStringLiteral("org.kde.keyboard");
static const QString s_keyboardObject = QStringLiteral("/Layouts");

KeyboardLayoutDBusInterface::KeyboardLayoutDBusInterface(Xkb *xkb, KeyboardLayout *parent)
    : QObject(parent)
    , m_xkb(xkb)
    , m_keyboardLayout(parent)
{
    QDBusConnection::sessionBus().registerService(s_keyboardService);
    QDBusConnection::sessionBus().registerObject(s_keyboardObject, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
}

KeyboardLayoutDBusInterface::~KeyboardLayoutDBusInterface()
{
    QDBusConnection::sessionBus().unregisterService(s_keyboardService);
}

void KeyboardLayoutDBusInterface::switchToNextLayout()
{
    m_keyboardLayout->switchToNextLayout();
}

void KeyboardLayoutDBusInterface::switchToPreviousLayout()
{
    m_keyboardLayout->switchToPreviousLayout();
}

bool KeyboardLayoutDBusInterface::setLayout(const QString &layout)
{
    const auto layouts = m_xkb->layoutNames();
    auto it = layouts.begin();
    for (; it !=layouts.end(); it++) {
        if (it.value() == layout) {
            break;
        }
    }
    if (it == layouts.end()) {
        return false;
    }
    const quint32 previousLayout = m_xkb->currentLayout();
    m_xkb->switchToLayout(it.key());
    m_keyboardLayout->checkLayoutChange(previousLayout);
    return true;
}

QString KeyboardLayoutDBusInterface::getLayout() const
{
    return m_xkb->layoutName();
}

QString KeyboardLayoutDBusInterface::getLayoutDisplayName() const
{
    return m_xkb->layoutShortName();
}

QString KeyboardLayoutDBusInterface::getLayoutLongName() const
{
    return translatedLayout(m_xkb->layoutName());
}

QStringList KeyboardLayoutDBusInterface::getLayoutsList() const
{
    const auto layouts = m_xkb->layoutNames();
    QStringList ret;
    for (auto it = layouts.begin(); it != layouts.end(); it++) {
        ret << it.value();
    }
    return ret;
}

}
