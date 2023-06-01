/*
SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
    FtraceImpl::instance().print(message);
}
void begin(const QString& message, ulong ctx)
{
    FtraceImpl::instance().printBegin(message, ctx);
}
void end(const QString& message, ulong ctx)
{
    FtraceImpl::instance().printEnd(message, ctx);
}

bool setEnabled(bool enable)
{
    return FtraceImpl::instance().setEnabled(enable);
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

bool setEnabled(bool enable)
{
    // Report error iff trying to enable.
    return !enable;
}
#endif

}
}
}
