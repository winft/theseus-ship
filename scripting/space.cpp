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
#include <QDesktopWidget>

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

void space::setupAbstractClientConnections(window* window)
{
    connect(window, &window::clientMinimized, this, &space::clientMinimized);
    connect(window, &window::clientUnminimized, this, &space::clientUnminimized);
    connect(window, &window::clientMaximizedStateChanged, this, &space::clientMaximizeSet);
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

int space::screenAt(const QPointF& pos) const
{
    auto const& outputs = base::singleton_interface::platform->get_outputs();
    auto output = base::get_nearest_output(outputs, pos.toPoint());
    if (!output) {
        return 0;
    }
    return base::get_output_index(outputs, *output);
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
                                    this,
                                    &declarative_script_space::countClientList,
                                    &declarative_script_space::atClientList);
}

int declarative_script_space::countClientList(QQmlListProperty<window>* clients)
{
    Q_UNUSED(clients)
    auto wsw = reinterpret_cast<declarative_script_space*>(clients->data);
    return wsw->windows_count;
}

window* declarative_script_space::atClientList(QQmlListProperty<window>* clients, int index)
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
