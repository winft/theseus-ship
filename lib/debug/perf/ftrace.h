/*
SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <QString>

namespace KWin
{
namespace Perf
{

namespace Ftrace
{

/**
 * Internal perf API for consumers
 */
void KWIN_EXPORT mark(const QString& message);
void KWIN_EXPORT begin(const QString& message, ulong ctx);
void KWIN_EXPORT end(const QString& message, ulong ctx);

bool KWIN_EXPORT setEnabled(bool enable);

}
}
}
