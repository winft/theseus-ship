/*
    SPDX-FileCopyrightText: 2019 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#include "windowsystem.h"

#include <win/singleton_interface.h>

#include <KWaylandExtras>
#include <KWindowSystem>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>

Q_DECLARE_METATYPE(NET::WindowType)

namespace KWin
{

WindowSystem::WindowSystem()
    : QObject()
    , KWindowSystemPrivateV2()
{
}

void WindowSystem::activateWindow(QWindow* /*win*/, long int /*time*/)
{
    // KWin cannot activate own windows
}

void WindowSystem::setShowingDesktop(bool /*showing*/)
{
    // KWin should not use KWindowSystem to set showing desktop state
}

bool WindowSystem::showingDesktop()
{
    // KWin should not use KWindowSystem for showing desktop state
    return false;
}

void WindowSystem::requestToken(QWindow* win, uint32_t serial, const QString& appId)
{
    // It's coming from within kwin, it doesn't matter the window.

    auto& token_setter = win::singleton_interface::set_activation_token;
    assert(token_setter);
    auto token = token_setter(appId.toStdString());

    // Ensure that xdgActivationTokenArrived is always emitted asynchronously
    QTimer::singleShot(0, [serial, token] {
        Q_EMIT KWaylandExtras::self()->xdgActivationTokenArrived(serial,
                                                                 QString::fromStdString(token));
    });
}

void WindowSystem::setCurrentToken(QString const& /*token*/)
{
    // KWin cannot activate own windows
}

quint32 WindowSystem::lastInputSerial(QWindow* /*window*/)
{
    // TODO(romangg): It's assumed this is not required for internal windows. Or is it?
    return 0;
}

void WindowSystem::exportWindow(QWindow* window)
{
    Q_UNUSED(window);
}

void WindowSystem::unexportWindow(QWindow* window)
{
    Q_UNUSED(window);
}

void WindowSystem::setMainWindow(QWindow* window, const QString& handle)
{
    Q_UNUSED(window);
    Q_UNUSED(handle);
}

}
