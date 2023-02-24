/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QKeySequence>

namespace Utils
{

QKeySequence reverseKey(QKeySequence const& key);

QKeySequence cropKey(QKeySequence const& key, int count);

bool contains(QKeySequence const& key, QKeySequence const& other);

bool matchSequences(QKeySequence const& key, const QList<QKeySequence>& keys);

QKeySequence mangleKey(QKeySequence const& key);

}
