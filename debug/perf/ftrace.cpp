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
#include "ftrace.h"

#include "config-kwin.h"

#if HAVE_PERF
#include "ftrace_impl.h"
#endif

namespace KWin
{
namespace Perf
{
namespace Ftrace
{

#if HAVE_PERF
void mark(const QString& message)
{
    FtraceImpl::self()->print(message);
}
void begin(const QString& message, ulong ctx)
{
    FtraceImpl::self()->printBegin(message, ctx);
}
void end(const QString& message, ulong ctx)
{
    FtraceImpl::self()->printEnd(message, ctx);
}

bool valid(QObject* parent, bool create)
{
    if (FtraceImpl::self()) {
        return true;
    }
    if (!create) {
        return false;
    }
    return FtraceImpl::create(parent) != nullptr;
}

bool setEnabled(bool enable)
{
    return FtraceImpl::self()->setEnabled(enable);
}
#else
void mark(const QString& message)
{
    Q_UNUSED(message)
}
void begin(const QString& message, ulong ctx)
{
    Q_UNUSED(message)
    Q_UNUSED(ctx)
}
void end(const QString& message, ulong ctx)
{
    Q_UNUSED(message)
    Q_UNUSED(ctx)
}

bool valid(QObject* parent, bool create)
{
    Q_UNUSED(parent)
    Q_UNUSED(create)
    return false;
}
#endif

}
}
}
