/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "kwin_export.h"

#include <KConfigWatcher>

namespace KWin::base
{

class KWIN_EXPORT options_qobject : public QObject
{
    Q_OBJECT

public:
    options_qobject(base::operation_mode mode);
    base::operation_mode windowing_mode;

Q_SIGNALS:
    void configChanged();
};

class KWIN_EXPORT options
{
public:
    options(base::operation_mode mode, KSharedConfigPtr config);
    ~options();

    void updateSettings();
    QStringList modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const;

    std::unique_ptr<options_qobject> qobject;

private:
    KSharedConfigPtr config;
    KConfigWatcher::Ptr m_configWatcher;

    QHash<Qt::KeyboardModifier, QStringList> m_modifierOnlyShortcuts;
};

inline std::unique_ptr<options> create_options(operation_mode mode, KSharedConfigPtr config)
{
    return std::make_unique<base::options>(mode, config);
}

}
