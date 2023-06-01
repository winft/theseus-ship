/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_window.h"

#include "effects_handler.h"

namespace KWin
{

class Q_DECL_HIDDEN EffectWindow::Private
{
public:
    Private(EffectWindow* q);

    EffectWindow* q;
};

EffectWindow::Private::Private(EffectWindow* q)
    : q(q)
{
}

EffectWindow::EffectWindow(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
}

EffectWindow::~EffectWindow()
{
}

bool EffectWindow::isOnActivity(const QString& activity) const
{
    const QStringList _activities = activities();
    return _activities.isEmpty() || _activities.contains(activity);
}

bool EffectWindow::isOnAllActivities() const
{
    return activities().isEmpty();
}

void EffectWindow::setMinimized(bool min)
{
    if (min) {
        minimize();
    } else {
        unminimize();
    }
}

bool EffectWindow::isOnCurrentActivity() const
{
    return isOnActivity(effects->currentActivity());
}

bool EffectWindow::isOnCurrentDesktop() const
{
    return isOnDesktop(effects->currentDesktop());
}

bool EffectWindow::isOnDesktop(int d) const
{
    const QVector<uint> ds = desktops();
    return ds.isEmpty() || ds.contains(d);
}

bool EffectWindow::isOnAllDesktops() const
{
    return desktops().isEmpty();
}

bool EffectWindow::hasDecoration() const
{
    return contentsRect() != QRect(0, 0, width(), height());
}

bool EffectWindow::isVisible() const
{
    return !isMinimized() && isOnCurrentDesktop() && isOnCurrentActivity();
}

}
