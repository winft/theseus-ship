/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/singleton_interface.h>
#include <win/subspace.h>
#include <win/subspace_grid.h>
#include <win/subspace_manager.h>
#include <win/subspaces_get.h>
#include <win/subspaces_set.h>
#include <win/x11/net/root_info.h>

#include <KSharedConfig>
#include <QAction>
#include <QPointF>
#include <vector>

namespace KWin::win::x11
{

struct subspace_manager_backend {
    QString get_subspace_name(uint x11id) const
    {
        assert(data);
        return QString::fromUtf8(data->desktopName(x11id));
    }

    void get_layout(uint& columns, uint& rows, Qt::Orientation& orientation) const
    {
        if (!data) {
            return;
        }

        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = data->desktopLayoutColumnsRows().width();
        rows = std::max<int>(1, data->desktopLayoutColumnsRows().height());
        orientation = data->desktopLayoutOrientation() == x11::net::OrientationHorizontal
            ? Qt::Horizontal
            : Qt::Vertical;
    }

    void update_subspace_meta(size_t x11id, QString const& name)
    {
        if (data) {
            data->setDesktopName(x11id, name.toUtf8().data());
        }
    }

    void set_layout(uint columns, uint rows)
    {
        if (data) {
            data->setDesktopLayout(x11::net::OrientationHorizontal,
                                   columns,
                                   rows,
                                   x11::net::DesktopLayoutCornerTopLeft);
            data->activate();
        }
    }

    void set_current(uint x11id)
    {
        data->setCurrentDesktop(x11id);
    }

    void update_size(size_t size)
    {
        if (!data) {
            return;
        }

        data->setNumberOfDesktops(size);

        auto viewports = new x11::net::point[size];
        data->setDesktopViewport(size, *viewports);
        delete[] viewports;
    }

    x11::net::root_info* data{nullptr};
};

class subspace_manager
{
public:
    subspace_manager()
        : qobject{std::make_unique<subspace_manager_qobject>()}
        , singleton{subspace_manager_create_singleton(*this)}

    {
        singleton_interface::subspaces = singleton.get();

        swipe_gesture.released_x = std::make_unique<QAction>();
        swipe_gesture.released_y = std::make_unique<QAction>();
    }

    ~subspace_manager()
    {
        singleton_interface::subspaces = {};
    }

    std::unique_ptr<subspace_manager_qobject> qobject;

    std::vector<subspace*> subspaces;
    uint rows{2};
    subspace_grid grid;
    subspace* current{nullptr};
    bool nav_wraps{false};

    struct {
        std::unique_ptr<QAction> released_x;
        std::unique_ptr<QAction> released_y;
    } swipe_gesture;
    QPointF current_desktop_offset{0, 0};

    subspace_manager_backend backend;
    KSharedConfig::Ptr config;
    static constexpr size_t max_count{20};

private:
    std::unique_ptr<subspaces_singleton> singleton;
};

}
