/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

static const int maxSequenceLength = 4;

#include <QKeySequence>
#include <QList>
#include <QString>

class KGlobalShortcutInfoPrivate
{
public:
    QString contextUniqueName;
    QString contextFriendlyName;
    QString componentUniqueName;
    QString componentFriendlyName;
    QString uniqueName;
    QString friendlyName;
    QList<QKeySequence> keys;
    QList<QKeySequence> defaultKeys;
};
