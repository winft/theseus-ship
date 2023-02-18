/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kglobalaccel_plugin.h"

#include "input/platform_qobject.h"
#include "input/singleton_interface.h"

#include <QDebug>

KGlobalAccelImpl::KGlobalAccelImpl(QObject* parent)
    : KGlobalAccelInterfaceV2(parent)
{
}

KGlobalAccelImpl::~KGlobalAccelImpl() = default;

bool KGlobalAccelImpl::grabKey(int key, bool grab)
{
    Q_UNUSED(key)
    Q_UNUSED(grab)
    return true;
}

void KGlobalAccelImpl::setEnabled(bool enabled)
{
    if (m_shuttingDown) {
        return;
    }
    auto input = KWin::input::singleton_interface::platform_qobject;
    if (!input) {
        qFatal("This plugin is intended to be used with KWin and this is not KWin, exiting now");
    } else {
        if (!m_inputDestroyedConnection) {
            m_inputDestroyedConnection
                = connect(input, &QObject::destroyed, this, [this] { m_shuttingDown = true; });
        }
    }

    if (input->register_global_accel) {
        input->register_global_accel(enabled ? this : nullptr);
    } else {
        qFatal("Input platform does not support KGlobalAccel");
    }
}

bool KGlobalAccelImpl::checkKeyPressed(int keyQt)
{
    return keyPressed(keyQt);
}

bool KGlobalAccelImpl::checkKeyReleased(int keyQt)
{
    return keyReleased(keyQt);
}
