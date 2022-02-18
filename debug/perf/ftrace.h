/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2020 Roman Gilg <subdiff@gmail.com>

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

bool valid(QObject* parent = nullptr, bool create = false);
bool setEnabled(bool enable);

}
}
}
