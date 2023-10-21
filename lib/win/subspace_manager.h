/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "singleton_interface.h"
#include <win/subspace.h>
#include <win/subspace_grid.h>
#include <win/subspaces_get.h>
#include <win/subspaces_set.h>

#include "kwin_export.h"

#include <KConfig>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <vector>

class KLocalizedString;
class Options;

namespace Wrapland::Server
{
class PlasmaVirtualDesktopManager;
}

namespace KWin::win
{

namespace x11::net
{
class root_info;
}

class KWIN_EXPORT subspace_manager_qobject : public QObject
{
    Q_OBJECT
public:
    subspace_manager_qobject();

Q_SIGNALS:
    void countChanged(uint previousCount, uint newCount);
    void rowsChanged(uint rows);

    void subspace_created(KWin::win::subspace*);
    void subspace_removed(KWin::win::subspace*);

    void current_changed(KWin::win::subspace* prev, KWin::win::subspace* next);

    /**
     * For realtime subspace switching animations. Offset is current total change in subspace
     * coordinate. x and y are negative if switching left/down. Example: x = 0.6 means 60% of the
     * way to the subspace to the right.
     */
    void current_changing(KWin::win::subspace* current, QPointF offset);
    void current_changing_cancelled();

    void layoutChanged(int columns, int rows);
    void nav_wraps_changed();
};

class KWIN_EXPORT subspace_manager
{
public:
    subspace_manager();
    ~subspace_manager();

    void setRootInfo(x11::net::root_info* info);

    uint rows() const;

    QString name(uint sub) const;

    /**
     * Create a new virtual desktop at the requested position. The difference with setCount is that
     * setCount always adds new subspaces at the end of the chain. The Id is automatically
     * generated.
     * @returns the new subspace, nullptr if we reached the maximum number of subspaces.
     */
    subspace* create_subspace(uint position, QString const& name);

    void remove_subspace(subspace* sub);

    void setCount(uint count);

    void setRows(uint rows);
    void updateLayout();

    void load();
    void save();

    std::unique_ptr<subspace_manager_qobject> qobject;
    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement{nullptr};

    std::vector<subspace*> subspaces;
    subspace_grid grid;
    subspace* current{nullptr};
    bool nav_wraps{false};

    struct {
        std::unique_ptr<QAction> released_x;
        std::unique_ptr<QAction> released_y;
    } swipe_gesture;
    QPointF current_desktop_offset{0, 0};

    x11::net::root_info* root_info{nullptr};
    KSharedConfig::Ptr config;
    static constexpr size_t max_count{20};

private:
    void updateRootInfo();

    subspace* add_subspace(size_t position, QString const& id, QString const& name);

    uint m_rows{2};

    subspaces_singleton singleton;
};

inline QString subspace_manager_get_default_subspace_name(int x11id)
{
    return i18n("Desktop %1", x11id);
}

template<typename Manager>
void subspace_manager_update_subspace_meta(Manager& mgr,
                                           subspace* subsp,
                                           QString const& name,
                                           size_t x11id)
{
    subsp->setName(name);
    subsp->setX11DesktopNumber(x11id);

    if (mgr.root_info) {
        mgr.root_info->setDesktopName(x11id, name.toUtf8().data());
    }
}

template<typename Manager>
void subspace_manager_set_nav_wraps(Manager& mgr, bool enabled)
{
    if (enabled == mgr.nav_wraps) {
        return;
    }

    mgr.nav_wraps = enabled;
    Q_EMIT mgr.qobject->nav_wraps_changed();
}

template<typename Manager>
void subspace_manager_shrink_subspaces(Manager& mgr, uint count)
{
    if (count >= mgr.subspaces.size()) {
        return;
    }

    auto const subspaces_to_remove
        = std::vector<subspace*>{mgr.subspaces.begin() + count, mgr.subspaces.end()};
    mgr.subspaces.resize(count);

    assert(mgr.current);
    auto old_subsp = mgr.current;
    auto old_current = subspaces_get_current_x11id(mgr);
    auto new_current = std::min<uint>(old_current, count);

    mgr.current = mgr.subspaces.at(new_current - 1);

    if (old_current != new_current) {
        Q_EMIT mgr.qobject->current_changed(old_subsp, mgr.current);
    }

    for (auto desktop : subspaces_to_remove) {
        Q_EMIT mgr.qobject->subspace_removed(desktop);
        desktop->deleteLater();
    }
}

template<typename Manager>
void subspace_manager_connect_gestures(Manager& mgr)
{
    static constexpr double gesture_switch_threshold{.25};

    QObject::connect(
        mgr.swipe_gesture.released_x.get(), &QAction::triggered, mgr.qobject.get(), [&]() {
            // Note that if desktop wrapping is disabled and there's no desktop to left or right,
            // toLeft() and toRight() will return the current desktop.
            auto target = mgr.current;

            if (mgr.current_desktop_offset.x() <= -gesture_switch_threshold) {
                target = &subspaces_get_west_of_current(mgr);
            } else if (mgr.current_desktop_offset.x() >= gesture_switch_threshold) {
                target = &subspaces_get_east_of_current(mgr);
            }

            // If the current subspace has not changed, consider that the gesture has been canceled.
            if (mgr.current != target) {
                subspaces_set_current(mgr, *target);
            } else {
                Q_EMIT mgr.qobject->current_changing_cancelled();
            }

            mgr.current_desktop_offset = {0, 0};
        });

    QObject::connect(
        mgr.swipe_gesture.released_y.get(), &QAction::triggered, mgr.qobject.get(), [&]() {
            auto target = mgr.current;

            if (mgr.current_desktop_offset.y() <= -gesture_switch_threshold) {
                target = &subspaces_get_north_of_current(mgr);
            } else if (mgr.current_desktop_offset.y() >= gesture_switch_threshold) {
                target = &subspaces_get_south_of_current(mgr);
            }

            // If the current subspace has not changed, consider that the gesture has been canceled.
            if (mgr.current != target) {
                subspaces_set_current(mgr, *target);
            } else {
                Q_EMIT mgr.qobject->current_changing_cancelled();
            }

            mgr.current_desktop_offset = {0, 0};
        });
}

}
