/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwinscreenedge.h"

#include "monitor.h"

namespace KWin
{

KWinScreenEdge::KWinScreenEdge(QWidget *parent)
    : QWidget(parent)
{
    QMetaObject::invokeMethod(this, "createConnection", Qt::QueuedConnection);
}

KWinScreenEdge::~KWinScreenEdge()
{
}

void KWinScreenEdge::monitorHideEdge(win::electric_border border, bool hidden)
{
    auto const edge = KWinScreenEdge::electricBorderToMonitorEdge(border);
    monitor()->setEdgeHidden(edge, hidden);
    if(edge != Monitor::None) {
        monitor()->setEdgeHidden(edge, hidden);
    }
}

void KWinScreenEdge::monitorEnableEdge(win::electric_border border, bool enabled)
{
    const int edge = KWinScreenEdge::electricBorderToMonitorEdge(border);
    monitor()->setEdgeEnabled(edge, enabled);
}

void KWinScreenEdge::monitorAddItem(const QString &item)
{
    for (int i = 0; i < 8; i++) {
        monitor()->addEdgeItem(i, item);
    }
}

void KWinScreenEdge::monitorItemSetEnabled(int index, bool enabled)
{
    for (int i = 0; i < 8; i++) {
        monitor()->setEdgeItemEnabled(i, index, enabled);
    }
}

void KWinScreenEdge::monitorChangeEdge(const QList<int> &borderList, int index)
{
    for (int border : borderList) {
        monitorChangeEdge(static_cast<win::electric_border>(border), index);
    }
}

void KWinScreenEdge::monitorChangeEdge(win::electric_border border, int index)
{
    if (win::electric_border::_COUNT == border || win::electric_border::none == border) {
        return;
    }
    m_reference[border] = index;
    monitor()->selectEdgeItem(KWinScreenEdge::electricBorderToMonitorEdge(border), index);
}

QList<win::electric_border> KWinScreenEdge::monitorCheckEffectHasEdge(int index) const
{
    QList<win::electric_border> list;
    if (monitor()->selectedEdgeItem(Monitor::Top) == index) {
        list.append(win::electric_border::top);
    }
    if (monitor()->selectedEdgeItem(Monitor::TopRight) == index) {
        list.append(win::electric_border::top_right);
    }
    if (monitor()->selectedEdgeItem(Monitor::Right) == index) {
        list.append(win::electric_border::right);
    }
    if (monitor()->selectedEdgeItem(Monitor::BottomRight) == index) {
        list.append(win::electric_border::bottom_right);
    }
    if (monitor()->selectedEdgeItem(Monitor::Bottom) == index) {
        list.append(win::electric_border::bottom);
    }
    if (monitor()->selectedEdgeItem(Monitor::BottomLeft) == index) {
        list.append(win::electric_border::bottom_left);
    }
    if (monitor()->selectedEdgeItem(Monitor::Left) == index) {
        list.append(win::electric_border::left);
    }
    if (monitor()->selectedEdgeItem(Monitor::TopLeft) == index) {
        list.append(win::electric_border::top_left);
    }

    if (list.isEmpty()) {
        list.append(win::electric_border::none);
    }
    return list;
}

QList<int> KWinScreenEdge::monitorCheckEffectHasEdgeInt(int index) const
{
    QList<int> ret;
    auto const orig = monitorCheckEffectHasEdge(index);
    for (auto border : orig) {
        ret << static_cast<int>(border);
    }
    return ret;
}

int KWinScreenEdge::selectedEdgeItem(win::electric_border border) const
{
    return monitor()->selectedEdgeItem(KWinScreenEdge::electricBorderToMonitorEdge(border));
}

void KWinScreenEdge::monitorChangeDefaultEdge(win::electric_border border, int index)
{
    if (win::electric_border::_COUNT == border || win::electric_border::none == border) {
        return;
    }
    m_default[border] = index;
}

void KWinScreenEdge::monitorChangeDefaultEdge(const QList<int> &borderList, int index)
{
    for (int border : borderList) {
        monitorChangeDefaultEdge(static_cast<win::electric_border>(border), index);
    }
}

void KWinScreenEdge::reload()
{
    for (auto it = m_reference.cbegin(); it != m_reference.cend(); ++it) {
        monitor()->selectEdgeItem(KWinScreenEdge::electricBorderToMonitorEdge(it.key()), it.value());
    }
    onChanged();
}

void KWinScreenEdge::setDefaults()
{
    for (auto it = m_default.cbegin(); it != m_default.cend(); ++it) {
        monitor()->selectEdgeItem(KWinScreenEdge::electricBorderToMonitorEdge(it.key()), it.value());
    }
    onChanged();
}

int KWinScreenEdge::electricBorderToMonitorEdge(win::electric_border border)
{
    switch(border) {
    case win::electric_border::top:
        return Monitor::Top;
    case win::electric_border::top_right:
        return Monitor::TopRight;
    case win::electric_border::right:
        return Monitor::Right;
    case win::electric_border::bottom_right:
        return Monitor::BottomRight;
    case win::electric_border::bottom:
        return Monitor::Bottom;
    case win::electric_border::bottom_left:
        return Monitor::BottomLeft;
    case win::electric_border::left:
        return Monitor::Left;
    case win::electric_border::top_left:
        return Monitor::TopLeft;
    default: // ELECTRIC_COUNT and ElectricNone
        return Monitor::None;
    }
}

void KWinScreenEdge::onChanged()
{
    bool needSave = isSaveNeeded();
    for (auto it = m_reference.cbegin(); it != m_reference.cend(); ++it) {
        auto const edge = KWinScreenEdge::electricBorderToMonitorEdge(it.key());
        if(edge != Monitor::None) {
            needSave |= it.value() != monitor()->selectedEdgeItem(edge);
        }
    }
    Q_EMIT saveNeededChanged(needSave);

    bool defaults = isDefault();
    for (auto it = m_default.cbegin(); it != m_default.cend(); ++it) {
        auto const edge = KWinScreenEdge::electricBorderToMonitorEdge(it.key());
        if(edge != Monitor::None) {
            defaults &= it.value() == monitor()->selectedEdgeItem(edge);
        }
    }
    Q_EMIT defaultChanged(defaults);
}

void KWinScreenEdge::createConnection()
{
        connect(monitor(),
                &Monitor::changed,
                this,
                &KWinScreenEdge::onChanged);
}

bool KWinScreenEdge::isSaveNeeded() const
{
    return false;
}

bool KWinScreenEdge::isDefault() const
{
    return true;
}

} // namespace
