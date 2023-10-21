/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "singleton_interface.h"
#include <win/subspace.h>
#include <win/subspace_grid.h>

#include "kwin_export.h"

#include <KConfig>
#include <KSharedConfig>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <vector>

class KLocalizedString;
class QAction;
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
    void setConfig(KSharedConfig::Ptr config);

    uint rows() const;

    uint current_x11id() const;

    QString name(uint sub) const;
    bool get_nav_wraps() const;
    const subspace_grid& grid() const;

    subspace& get_north_of_current() const;
    uint get_north_of(uint id, bool wrap) const;
    subspace& get_north_of(subspace& desktop, bool wrap) const;

    subspace& get_east_of_current() const;
    uint get_east_of(uint id, bool wrap) const;
    subspace& get_east_of(subspace& desktop, bool wrap) const;

    subspace& get_south_of_current() const;
    uint get_south_of(uint id, bool wrap) const;
    subspace& get_south_of(subspace& desktop, bool wrap) const;

    subspace& get_west_of_current() const;
    uint get_west_of(uint id, bool wrap) const;
    subspace& get_west_of(subspace& desktop, bool wrap) const;

    subspace& get_successor_of_current() const;
    subspace& get_successor_of(subspace& desktop, bool wrap) const;
    uint get_successor_of(uint id, bool wrap) const;

    subspace& get_predecessor_of_current() const;
    subspace& get_predecessor_of(subspace& desktop, bool wrap) const;
    uint get_predecessor_of(uint id, bool wrap) const;

    subspace* subspace_for_x11id(uint id) const;
    subspace* subspace_for_id(QString const& id) const;

    /**
     * Create a new virtual desktop at the requested position. The difference with setCount is that
     * setCount always adds new subspaces at the end of the chain. The Id is automatically
     * generated.
     * @returns the new subspace, nullptr if we reached the maximum number of subspaces.
     */
    subspace* create_subspace(uint position, QString const& name);

    void remove_subspace(QString const& id);
    void remove_subspace(subspace* sub);

    static uint maximum();

    void setCount(uint count);
    bool setCurrent(uint current);
    bool setCurrent(subspace& subsp);

    void setRows(uint rows);
    void updateLayout();
    void set_nav_wraps(bool enabled);

    void load();
    void save();

    std::unique_ptr<subspace_manager_qobject> qobject;

    void slotSwitchTo(QAction& action);
    void slotNext();
    void slotPrevious();
    void slotRight();
    void slotLeft();
    void slotUp();
    void slotDown();

    void connect_gestures();

    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement{nullptr};

    std::vector<subspace*> subspaces;
    subspace* current{nullptr};

    struct {
        std::unique_ptr<QAction> released_x;
        std::unique_ptr<QAction> released_y;
    } swipe_gesture;
    QPointF current_desktop_offset{0, 0};

private:
    void updateRootInfo();

    subspace* add_subspace(size_t position, QString const& id, QString const& name);
    void shrink_subspaces(uint count);

    QString defaultName(int desktop) const;

    uint m_rows{2};
    bool nav_wraps{false};
    subspace_grid m_grid;
    x11::net::root_info* m_rootInfo{nullptr};
    KSharedConfig::Ptr m_config;

    subspaces_singleton singleton;
};

inline uint subspace_manager::maximum()
{
    return 20;
}

inline void subspace_manager::setConfig(KSharedConfig::Ptr config)
{
    m_config = std::move(config);
}

inline subspace_grid const& subspace_manager::grid() const
{
    return m_grid;
}

}
