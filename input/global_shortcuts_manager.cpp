/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_manager.h"

#include "gestures.h"
#include "global_shortcut.h"
#include "logging.h"

#include "main.h"

#include <KGlobalAccel/private/kglobalaccel_interface.h>
#include <KGlobalAccel/private/kglobalacceld.h>

#include <QAction>

namespace KWin::input
{

global_shortcuts_manager::global_shortcuts_manager()
    : m_gestureRecognizer{std::make_unique<gesture_recognizer>()}
{
}

global_shortcuts_manager::~global_shortcuts_manager() = default;

void global_shortcuts_manager::init()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        qputenv("KGLOBALACCELD_PLATFORM", QByteArrayLiteral("org.kde.kwin"));
        m_kglobalAccel = new KGlobalAccelD(this);
        if (!m_kglobalAccel->init()) {
            qCDebug(KWIN_INPUT) << "Init of kglobalaccel failed";
            delete m_kglobalAccel;
            m_kglobalAccel = nullptr;
        } else {
            qCDebug(KWIN_INPUT) << "KGlobalAcceld inited";
        }
    }
}

void global_shortcuts_manager::objectDeleted(QObject* object)
{
    auto it = m_shortcuts.begin();
    while (it != m_shortcuts.end()) {
        if (it->action() == object) {
            it = m_shortcuts.erase(it);
        } else {
            ++it;
        }
    }
}

bool global_shortcuts_manager::addIfNotExists(global_shortcut sc)
{
    for (const auto& cs : m_shortcuts) {
        if (sc.shortcut() == cs.shortcut()) {
            return false;
        }
    }

    if (std::holds_alternative<FourFingerSwipeShortcut>(sc.shortcut())) {
        m_gestureRecognizer->registerGesture(sc.swipeGesture());
    }
    QObject::connect(
        sc.action(), &QAction::destroyed, this, &global_shortcuts_manager::objectDeleted);
    m_shortcuts.push_back(std::move(sc));
    return true;
}

void global_shortcuts_manager::registerPointerShortcut(QAction* action,
                                                       Qt::KeyboardModifiers modifiers,
                                                       Qt::MouseButtons pointerButtons)
{
    addIfNotExists(global_shortcut(PointerButtonShortcut{modifiers, pointerButtons}, action));
}

void global_shortcuts_manager::registerAxisShortcut(QAction* action,
                                                    Qt::KeyboardModifiers modifiers,
                                                    PointerAxisDirection axis)
{
    addIfNotExists(global_shortcut(PointerAxisShortcut{modifiers, axis}, action));
}

void global_shortcuts_manager::registerTouchpadSwipe(QAction* action, SwipeDirection direction)
{
    addIfNotExists(global_shortcut(FourFingerSwipeShortcut{direction}, action));
}

bool global_shortcuts_manager::processKey(Qt::KeyboardModifiers mods, int keyQt)
{
    if (m_kglobalAccelInterface) {
        if (!keyQt && !mods) {
            return false;
        }
        auto check = [this](Qt::KeyboardModifiers mods, int keyQt) {
            bool retVal = false;
            QMetaObject::invokeMethod(m_kglobalAccelInterface,
                                      "checkKeyPressed",
                                      Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, retVal),
                                      Q_ARG(int, int(mods) | keyQt));
            return retVal;
        };
        if (check(mods, keyQt)) {
            return true;
        } else if (keyQt == Qt::Key_Backtab) {
            // KGlobalAccel on X11 has some workaround for Backtab
            // see kglobalaccel/src/runtime/plugins/xcb/kglobalccel_x11.cpp method x11KeyPress
            // Apparently KKeySequenceWidget captures Shift+Tab instead of Backtab
            // thus if the key is backtab we should adjust to add shift again and use tab
            // in addition KWin registers the shortcut incorrectly as Alt+Shift+Backtab
            // this should be changed to either Alt+Backtab or Alt+Shift+Tab to match
            // KKeySequenceWidget trying the variants
            if (check(mods | Qt::ShiftModifier, keyQt)) {
                return true;
            }
            if (check(mods | Qt::ShiftModifier, Qt::Key_Tab)) {
                return true;
            }
        }
    }
    return false;
}

template<typename ShortcutKind, typename... Args>
bool match(QVector<global_shortcut>& shortcuts, Args... args)
{
    for (auto& sc : shortcuts) {
        if (std::holds_alternative<ShortcutKind>(sc.shortcut())) {
            if (std::get<ShortcutKind>(sc.shortcut()) == ShortcutKind{args...}) {
                sc.invoke();
                return true;
            }
        }
    }
    return false;
}

// TODO(C++20): use ranges for a nicer way of filtering by shortcut type
bool global_shortcuts_manager::processPointerPressed(Qt::KeyboardModifiers mods,
                                                     Qt::MouseButtons pointerButtons)
{
    return match<PointerButtonShortcut>(m_shortcuts, mods, pointerButtons);
}

bool global_shortcuts_manager::processAxis(Qt::KeyboardModifiers mods, PointerAxisDirection axis)
{
    return match<PointerAxisShortcut>(m_shortcuts, mods, axis);
}

void global_shortcuts_manager::processSwipeStart(uint fingerCount)
{
    m_gestureRecognizer->startSwipeGesture(fingerCount);
}

void global_shortcuts_manager::processSwipeUpdate(const QSizeF& delta)
{
    m_gestureRecognizer->updateSwipeGesture(delta);
}

void global_shortcuts_manager::processSwipeCancel()
{
    m_gestureRecognizer->cancelSwipeGesture();
}

void global_shortcuts_manager::processSwipeEnd()
{
    m_gestureRecognizer->endSwipeGesture();
    // TODO: cancel on Wayland Seat if one triggered
}

}
