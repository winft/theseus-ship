/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "switcheritem.h"

#include "base/platform.h"
#include "main.h"
#include "render/compositor.h"
#include "screens.h"
#include "tabboxhandler.h"
// Qt
#include <QAbstractItemModel>

namespace KWin
{
namespace TabBox
{

SwitcherItem::SwitcherItem(QObject* parent)
    : QObject(parent)
    , m_model(nullptr)
    , m_item(nullptr)
    , m_visible(false)
    , m_allDesktops(false)
    , m_currentIndex(0)
{
    m_selectedIndexConnection = connect(tabBox, &TabBoxHandler::selectedIndexChanged, [this] {
        if (isVisible()) {
            setCurrentIndex(tabBox->currentIndex().row());
        }
    });
    connect(&kwinApp()->get_base().screens,
            &Screens::changed,
            this,
            &SwitcherItem::screenGeometryChanged);
    connect(render::compositor::self(),
            &render::compositor::compositingToggled,
            this,
            &SwitcherItem::compositingChanged);
}

SwitcherItem::~SwitcherItem()
{
    disconnect(m_selectedIndexConnection);
}

void SwitcherItem::setItem(QObject* item)
{
    if (m_item == item) {
        return;
    }
    m_item = item;
    Q_EMIT itemChanged();
}

void SwitcherItem::setModel(QAbstractItemModel* model)
{
    m_model = model;
    Q_EMIT modelChanged();
}

void SwitcherItem::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    if (visible)
        Q_EMIT screenGeometryChanged();
    m_visible = visible;
    Q_EMIT visibleChanged();
}

QRect SwitcherItem::screenGeometry() const
{
    auto& screens = kwinApp()->get_base().screens;
    return screens.geometry(screens.current());
}

void SwitcherItem::setCurrentIndex(int index)
{
    if (m_currentIndex == index) {
        return;
    }
    m_currentIndex = index;
    if (m_model) {
        tabBox->setCurrentIndex(m_model->index(index, 0));
    }
    Q_EMIT currentIndexChanged(m_currentIndex);
}

void SwitcherItem::setAllDesktops(bool all)
{
    if (m_allDesktops == all) {
        return;
    }
    m_allDesktops = all;
    Q_EMIT allDesktopsChanged();
}

void SwitcherItem::setNoModifierGrab(bool set)
{
    if (m_noModifierGrab == set) {
        return;
    }
    m_noModifierGrab = set;
    Q_EMIT noModifierGrabChanged();
}

bool SwitcherItem::compositing()
{
    return render::compositor::compositing();
}

}
}
