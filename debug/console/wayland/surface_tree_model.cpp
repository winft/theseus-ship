/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "surface_tree_model.h"

#include "base/wayland/server.h"
#include "toplevel.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>

namespace KWin::debug
{

surface_tree_model::surface_tree_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    // TODO: it would be nice to not have to reset the model on each change
    auto reset = [this] {
        beginResetModel();
        endResetModel();
    };

    const auto unmangeds = workspace()->unmanagedList();
    for (auto u : unmangeds) {
        if (!u->surface()) {
            continue;
        }
        QObject::connect(
            u->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
    }
    for (auto c : workspace()->allClientList()) {
        if (!c->surface()) {
            continue;
        }
        QObject::connect(
            c->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
    }
    QObject::connect(static_cast<win::wayland::space*>(workspace()),
                     &win::wayland::space::wayland_window_added,
                     this,
                     [this, reset](auto win) {
                         QObject::connect(win->surface(),
                                          &Wrapland::Server::Surface::subsurfaceTreeChanged,
                                          this,
                                          reset);
                         reset();
                     });
    QObject::connect(workspace(), &win::space::clientAdded, this, [this, reset](auto c) {
        if (c->surface()) {
            QObject::connect(
                c->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
        }
        reset();
    });
    QObject::connect(workspace(), &win::space::clientRemoved, this, reset);
    QObject::connect(
        workspace(), &win::space::unmanagedAdded, this, [this, reset](Toplevel* window) {
            if (window->surface()) {
                QObject::connect(window->surface(),
                                 &Wrapland::Server::Surface::subsurfaceTreeChanged,
                                 this,
                                 reset);
            }
            reset();
        });
    QObject::connect(workspace(), &win::space::unmanagedRemoved, this, reset);
}

int surface_tree_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int surface_tree_model::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
            const auto& children = surface->state().children;
            return children.size();
        }
        return 0;
    }

    // toplevel are all windows
    return workspace()->allClientList().size() + workspace()->unmanagedList().size();
}

QModelIndex surface_tree_model::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0) {
        // invalid column
        return QModelIndex();
    }

    auto row_u = static_cast<size_t>(row);

    if (parent.isValid()) {
        if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
            const auto& children = surface->state().children;
            if (row_u < children.size()) {
                return createIndex(row_u, column, children.at(row_u)->surface());
            }
        }
        return QModelIndex();
    }

    // a window
    const auto& allClients = workspace()->allClientList();
    if (row_u < allClients.size()) {
        // references a client
        return createIndex(row_u, column, allClients.at(row_u)->surface());
    }

    int reference = allClients.size();
    const auto& unmanaged = workspace()->unmanagedList();
    if (row_u < reference + unmanaged.size()) {
        return createIndex(row_u, column, unmanaged.at(row_u - reference)->surface());
    }
    reference += unmanaged.size();

    // not found
    return QModelIndex();
}

QModelIndex surface_tree_model::parent(const QModelIndex& child) const
{
    if (auto surface = static_cast<Wrapland::Server::Surface*>(child.internalPointer())) {
        const auto& subsurface = surface->subsurface();
        if (!subsurface) {
            // doesn't reference a subsurface, this is a top-level window
            return QModelIndex();
        }
        auto parent = subsurface->parentSurface();
        if (!parent) {
            // something is wrong
            return QModelIndex();
        }
        // is the parent a subsurface itself?
        if (parent->subsurface()) {
            auto grandParent = parent->subsurface()->parentSurface();
            if (!grandParent) {
                // something is wrong
                return QModelIndex();
            }
            const auto& children = grandParent->state().children;
            for (size_t row = 0; row < children.size(); row++) {
                if (children.at(row) == parent->subsurface()) {
                    return createIndex(row, 0, parent);
                }
            }
            return QModelIndex();
        }
        // not a subsurface, thus it's a true window
        size_t row = 0;
        const auto& allClients = workspace()->allClientList();
        for (; row < allClients.size(); row++) {
            if (allClients.at(row)->surface() == parent) {
                return createIndex(row, 0, parent);
            }
        }
        row = allClients.size();
        const auto& unmanaged = workspace()->unmanagedList();
        for (size_t i = 0; i < unmanaged.size(); i++) {
            if (unmanaged.at(i)->surface() == parent) {
                return createIndex(row + i, 0, parent);
            }
        }
        row += unmanaged.size();
    }
    return QModelIndex();
}

QVariant surface_tree_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (auto surface = static_cast<Wrapland::Server::Surface*>(index.internalPointer())) {
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            return QStringLiteral("%1 (%2)")
                .arg(QString::fromStdString(surface->client()->executablePath()))
                .arg(surface->client()->processId());
        } else if (role == Qt::DecorationRole) {
            if (auto buffer = surface->state().buffer) {
                if (buffer->shmBuffer()) {
                    return buffer->shmImage()->createQImage().scaled(QSize(64, 64),
                                                                     Qt::KeepAspectRatio);
                }
            }
        }
    }
    return QVariant();
}

}
