/*
SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
SPDX-FileCopyrightText: 2011, 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "base/singleton_interface.h"
#include "singleton_interface.h"

#include "base/logging.h"
#include "render/outline.h"

#include <QApplication>

namespace KWin::scripting
{

QStringList space::activityList() const
{
    return {};
}

QSize space::workspaceSize() const
{
    return QSize(workspaceWidth(), workspaceHeight());
}

int space::displayWidth() const
{
    return displaySize().width();
}

int space::displayHeight() const
{
    return displaySize().height();
}

void space::showOutline(int x, int y, int width, int height)
{
    showOutline(QRect(x, y, width, height));
}

window* space::getClient(qulonglong windowId)
{
    return get_client_impl(windowId);
}

int space::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int space::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int space::workspaceHeight() const
{
    return desktopGridHeight() * displayHeight();
}

int space::workspaceWidth() const
{
    return desktopGridWidth() * displayWidth();
}

QRect space::virtualScreenGeometry() const
{
    return QRect({}, displaySize());
}

QSize space::virtualScreenSize() const
{
    return displaySize();
}

qt_script_space::qt_script_space()
{
    singleton_interface::qt_script_space = this;
}

qt_script_space::~qt_script_space()
{
    singleton_interface::qt_script_space = nullptr;
}

QList<window*> qt_script_space::clientList() const
{
    QList<window*> ret;
    for (auto const& window : windows()) {
        ret << window;
    }
    return ret;
}

QQmlListProperty<window> declarative_script_space::clients()
{
    return QQmlListProperty<window>(this,
                                    nullptr,
                                    &declarative_script_space::countClientList,
                                    &declarative_script_space::atClientList);
}

qsizetype declarative_script_space::countClientList(QQmlListProperty<window>* clients)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);
    return wsw->windows_count;
}

window* declarative_script_space::atClientList(QQmlListProperty<window>* clients, qsizetype index)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);

    try {
        return wsw->windows()[index];
    } catch (std::out_of_range const& ex) {
        return nullptr;
    }
}

}
