/*
    SPDX-FileCopyrightText: 2019 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#include "plugin.h"
#include "windoweffects.h"
#include "windowsystem.h"

KWindowSystemKWinPlugin::KWindowSystemKWinPlugin(QObject* parent)
    : KWindowSystemPluginInterface(parent)
{
}

KWindowSystemKWinPlugin::~KWindowSystemKWinPlugin()
{
}

KWindowEffectsPrivate* KWindowSystemKWinPlugin::createEffects()
{
    return new KWin::WindowEffects();
}

KWindowSystemPrivate* KWindowSystemKWinPlugin::createWindowSystem()
{
    return new KWin::WindowSystem();
}
