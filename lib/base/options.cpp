/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

namespace KWin::base
{

options_qobject::options_qobject(operation_mode mode)
    : windowing_mode{mode}
{
}

void options::updateSettings()
{
    Q_EMIT qobject->configChanged();
}

QStringList options::modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const
{
    return m_modifierOnlyShortcuts.value(mod);
}

options::options(operation_mode mode, KSharedConfigPtr config)
    : qobject{std::make_unique<options_qobject>(mode)}
    , config{config}
    , m_configWatcher{KConfigWatcher::create(config)}
{
}

options::~options() = default;

}
