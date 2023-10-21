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
#include <win/x11/net/root_info.h>

#include "kwin_export.h"

#include <KConfig>
#include <KConfigGroup>
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

    std::unique_ptr<subspace_manager_qobject> qobject;
    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement{nullptr};

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

    x11::net::root_info* root_info{nullptr};
    KSharedConfig::Ptr config;
    static constexpr size_t max_count{20};

private:
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
subspace*
subspace_manager_add_subspace(Manager& mgr, size_t position, QString const& id, QString const& name)
{
    auto subsp = new subspace(id, mgr.qobject.get());
    mgr.subspaces.insert(mgr.subspaces.begin() + position, subsp);
    subspace_manager_update_subspace_meta(mgr, subsp, name, position + 1);

    QObject::connect(subsp, &subspace::nameChanged, mgr.qobject.get(), [&, subsp]() {
        subspace_manager_update_subspace_meta(mgr, subsp, subsp->name(), subsp->x11DesktopNumber());
    });

    // update the id of displaced subspaces
    for (auto i = position + 1; i < mgr.subspaces.size(); ++i) {
        subspace_manager_update_subspace_meta(mgr, subsp, subsp->name(), i + 1);
    }

    return subsp;
}

template<typename Manager>
void subspace_manager_update_layout(Manager& mgr)
{
    mgr.rows = std::min<uint>(mgr.rows, mgr.subspaces.size());

    int columns = mgr.subspaces.size() / mgr.rows;
    auto orientation = Qt::Horizontal;

    if (mgr.root_info) {
        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = mgr.root_info->desktopLayoutColumnsRows().width();
        mgr.rows = std::max<int>(1, mgr.root_info->desktopLayoutColumnsRows().height());
        orientation = mgr.root_info->desktopLayoutOrientation() == x11::net::OrientationHorizontal
            ? Qt::Horizontal
            : Qt::Vertical;
    }

    if (columns == 0) {
        // Not given, set default layout
        mgr.rows = mgr.subspaces.size() == 1u ? 1 : 2;
        columns = mgr.subspaces.size() / mgr.rows;
    }

    // Patch to make desktop grid size equal 1 when 1 desktop for desktop switching animations
    if (mgr.subspaces.size() == 1) {
        mgr.rows = 1;
        columns = 1;
    }

    // Calculate valid grid size
    Q_ASSERT(columns > 0 || mgr.rows > 0);

    if ((columns <= 0) && (mgr.rows > 0)) {
        columns = (mgr.subspaces.size() + mgr.rows - 1) / mgr.rows;
    } else if ((mgr.rows <= 0) && (columns > 0)) {
        mgr.rows = (mgr.subspaces.size() + columns - 1) / columns;
    }

    while (columns * mgr.rows < mgr.subspaces.size()) {
        if (orientation == Qt::Horizontal) {
            ++columns;
        } else {
            ++mgr.rows;
        }
    }

    mgr.rows = std::max<uint>(1u, mgr.rows);
    mgr.grid.update(QSize(columns, mgr.rows), orientation, mgr.subspaces);

    // TODO: why is there no call to root_info->setDesktopLayout?
    Q_EMIT mgr.qobject->layoutChanged(columns, mgr.rows);
    Q_EMIT mgr.qobject->rowsChanged(mgr.rows);
}

template<typename Manager>
void subspace_manager_set_rows(Manager& mgr, uint rows)
{
    if (rows == 0 || rows > mgr.subspaces.size() || rows == mgr.rows) {
        return;
    }

    mgr.rows = rows;
    auto columns = mgr.subspaces.size() / rows;

    if (mgr.subspaces.size() % rows > 0) {
        columns++;
    }

    if (mgr.root_info) {
        mgr.root_info->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, rows, x11::net::DesktopLayoutCornerTopLeft);
        mgr.root_info->activate();
    }

    subspace_manager_update_layout(mgr);
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
void subspace_manager_update_root_info(Manager& mgr)
{
    if (!mgr.root_info) {
        return;
    }

    auto const n = mgr.subspaces.size();
    mgr.root_info->setNumberOfDesktops(n);

    auto viewports = new x11::net::point[n];
    mgr.root_info->setDesktopViewport(n, *viewports);
    delete[] viewports;
}

template<typename Manager>
void subspace_manager_set_root_info(Manager& mgr, x11::net::root_info* info)
{
    mgr.root_info = info;

    // Nothing will be connected to rootInfo
    if (!mgr.root_info) {
        return;
    }

    int columns = mgr.subspaces.size() / mgr.rows;
    if (mgr.subspaces.size() % mgr.rows > 0) {
        columns++;
    }

    mgr.root_info->setDesktopLayout(
        x11::net::OrientationHorizontal, columns, mgr.rows, x11::net::DesktopLayoutCornerTopLeft);

    subspace_manager_update_root_info(mgr);
    subspace_manager_update_layout(mgr);
    mgr.root_info->setCurrentDesktop(subspaces_get_current_x11id(mgr));

    for (auto vd : mgr.subspaces) {
        mgr.root_info->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
    }
}

template<typename Manager>
QString subspace_manager_get_subspace_name(Manager const& mgr, uint sub)
{
    if (mgr.subspaces.size() > sub - 1) {
        return mgr.subspaces.at(sub - 1)->name();
    }

    if (!mgr.root_info) {
        return subspace_manager_get_default_subspace_name(sub);
    }
    return QString::fromUtf8(mgr.root_info->desktopName(sub));
}

template<typename Manager>
void subspace_manager_load(Manager& mgr)
{
    if (!mgr.config) {
        return;
    }

    KConfigGroup group(mgr.config, QStringLiteral("Desktops"));

    auto const oldCount = mgr.subspaces.size();
    size_t const count = group.readEntry("Number", 1);

    subspace_manager_shrink_subspaces(mgr, count);

    auto set_name = [&](auto index) {
        auto const x11id = index + 1;
        QString name
            = group.readEntry(QStringLiteral("Name_%1").arg(x11id), i18n("Desktop %1", x11id));
        subspace_manager_update_subspace_meta(mgr, mgr.subspaces.at(index), name, x11id);
    };

    auto get_id = [&](auto index) {
        return group.readEntry(QStringLiteral("Id_%1").arg(index + 1), QString());
    };

    for (size_t index = 1; index < std::min(oldCount, count); index++) {
        assert(mgr.subspaces[index]->id() == get_id(index) || get_id(index).isEmpty());
        set_name(index);
    }

    for (size_t index = oldCount; index < count; index++) {
        subspace_manager_add_subspace(mgr, index, get_id(index), "");
        set_name(index);
    }

    assert(count == mgr.subspaces.size());

    subspace_manager_update_root_info(mgr);
    subspace_manager_update_layout(mgr);

    for (auto index = oldCount; index < mgr.subspaces.size(); index++) {
        Q_EMIT mgr.qobject->subspace_created(mgr.subspaces.at(index));
    }

    Q_EMIT mgr.qobject->countChanged(oldCount, mgr.subspaces.size());

    mgr.rows = std::clamp<int>(1, group.readEntry<int>("Rows", 2), mgr.subspaces.size());
}

template<typename Manager>
void subspace_manager_save(Manager& mgr)
{
    if (!mgr.config) {
        return;
    }

    KConfigGroup group(mgr.config, QStringLiteral("Desktops"));

    for (auto i = mgr.subspaces.size() + 1; group.hasKey(QStringLiteral("Id_%1").arg(i)); i++) {
        group.deleteEntry(QStringLiteral("Id_%1").arg(i));
        group.deleteEntry(QStringLiteral("Name_%1").arg(i));
    }

    group.writeEntry("Number", static_cast<int>(mgr.subspaces.size()));

    for (auto i = 1u; i <= mgr.subspaces.size(); ++i) {
        auto name = subspace_manager_get_subspace_name(mgr, i);
        auto const default_name = subspace_manager_get_default_subspace_name(i);

        if (name.isEmpty()) {
            name = default_name;
            subspace_manager_update_subspace_meta(mgr, mgr.subspaces.at(i - 1), name, i);
        }

        if (name != default_name) {
            group.writeEntry(QStringLiteral("Name_%1").arg(i), name);
        } else {
            auto stored_name = group.readEntry(QStringLiteral("Name_%1").arg(i), QString());
            if (stored_name != default_name) {
                group.deleteEntry(QStringLiteral("Name_%1").arg(i));
            }
        }
        group.writeEntry(QStringLiteral("Id_%1").arg(i), mgr.subspaces[i - 1]->id());
    }

    group.writeEntry("Rows", mgr.rows);

    // Save to disk
    group.sync();
}

template<typename Manager>
subspace* subspace_manager_create_subspace(Manager& mgr, uint position, QString const& name)
{
    if (mgr.subspaces.size() == subspace_manager::max_count) {
        // too many, can't insert new ones
        return nullptr;
    }

    position = std::clamp<uint>(0u, position, mgr.subspaces.size());

    auto desktopName = name;
    if (desktopName.isEmpty()) {
        desktopName = subspace_manager_get_default_subspace_name(position + 1);
    }

    auto vd = subspace_manager_add_subspace(mgr, position, "", desktopName);

    subspace_manager_save(mgr);
    subspace_manager_update_root_info(mgr);
    subspace_manager_update_layout(mgr);

    Q_EMIT mgr.qobject->subspace_created(vd);
    Q_EMIT mgr.qobject->countChanged(mgr.subspaces.size() - 1, mgr.subspaces.size());

    return vd;
}

template<typename Manager>
void subspace_manager_remove_subspace(Manager& mgr, subspace* sub)
{
    assert(sub);

    // don't end up without any subspace
    if (mgr.subspaces.size() == 1) {
        return;
    }

    assert(mgr.current);
    auto old_subsp = mgr.current;
    auto const oldCurrent = old_subsp->x11DesktopNumber();

    auto const i = sub->x11DesktopNumber() - 1;
    mgr.subspaces.erase(mgr.subspaces.begin() + i);

    for (auto j = i; j < mgr.subspaces.size(); ++j) {
        auto subsp = mgr.subspaces.at(j);
        subspace_manager_update_subspace_meta(mgr, subsp, subsp->name(), j + 1);
    }

    auto const newCurrent = std::min<uint>(oldCurrent, mgr.subspaces.size());
    mgr.current = mgr.subspaces.at(newCurrent - 1);

    if (oldCurrent != newCurrent) {
        Q_EMIT mgr.qobject->current_changed(old_subsp, mgr.current);
    }

    subspace_manager_update_root_info(mgr);
    subspace_manager_update_layout(mgr);
    subspace_manager_save(mgr);

    Q_EMIT mgr.qobject->subspace_removed(sub);
    Q_EMIT mgr.qobject->countChanged(mgr.subspaces.size() + 1, mgr.subspaces.size());

    sub->deleteLater();
}

template<typename Manager>
void subspace_manager_set_count(Manager& mgr, uint count)
{
    count = std::clamp<uint>(1, count, Manager::max_count);

    if (count == mgr.subspaces.size()) {
        // nothing to change
        return;
    }

    auto const oldCount = mgr.subspaces.size();

    subspace_manager_shrink_subspaces(mgr, count);

    while (mgr.subspaces.size() < count) {
        auto const position = mgr.subspaces.size();
        subspace_manager_add_subspace(
            mgr, position, "", subspace_manager_get_default_subspace_name(position + 1));
    }

    subspace_manager_update_root_info(mgr);
    subspace_manager_update_layout(mgr);
    subspace_manager_save(mgr);

    for (auto index = oldCount; index < mgr.subspaces.size(); index++) {
        Q_EMIT mgr.qobject->subspace_created(mgr.subspaces.at(index));
    }

    Q_EMIT mgr.qobject->countChanged(oldCount, mgr.subspaces.size());
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
