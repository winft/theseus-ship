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
#include <QPointer>
#include <QSize>

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

    void current_changed(uint prev, uint next);

    /**
     * For realtime subspace switching animations. Offset is current total change in subspace
     * coordinate. x and y are negative if switching left/down. Example: x = 0.6 means 60% of the
     * way to the subspace to the right.
     */
    void current_changing(uint current, QPointF offset);
    void current_changing_cancelled();

    void layoutChanged(int columns, int rows);
    void navigationWrappingAroundChanged();
};

class KWIN_EXPORT subspace_manager
{
public:
    subspace_manager();
    ~subspace_manager();

    void setRootInfo(x11::net::root_info* info);
    void setConfig(KSharedConfig::Ptr config);

    uint count() const;
    uint rows() const;

    uint current() const;
    subspace* current_subspace() const;

    QString name(uint sub) const;
    bool isNavigationWrappingAround() const;
    const subspace_grid& grid() const;

    uint above(uint id = 0, bool wrap = true) const;
    subspace* above(subspace* desktop, bool wrap = true) const;

    uint toRight(uint id = 0, bool wrap = true) const;
    subspace* toRight(subspace* desktop, bool wrap = true) const;

    uint below(uint id = 0, bool wrap = true) const;
    subspace* below(subspace* desktop, bool wrap = true) const;

    uint toLeft(uint id = 0, bool wrap = true) const;
    subspace* toLeft(subspace* desktop, bool wrap = true) const;

    subspace* next(subspace* desktop = nullptr, bool wrap = true) const;
    uint next(uint id = 0, bool wrap = true) const;

    subspace* previous(subspace* desktop = nullptr, bool wrap = true) const;
    uint previous(uint id = 0, bool wrap = true) const;

    QVector<subspace*> subspaces() const
    {
        return m_subspaces;
    }

    subspace* subspace_for_x11id(uint id) const;
    subspace* subspace_for_id(QString const& id) const;

    /**
     * Create a new virtual desktop at the requested position. The difference with setCount is that
     * setCount always adds new subspaces at the end of the chain. The Id is automatically
     * generated.
     * @returns the new subspace, nullptr if we reached the maximum number of subspaces.
     */
    subspace* create_subspace(uint position, QString const& name = QString());

    void remove_subspace(QString const& id);
    void remove_subspace(subspace* sub);

    static uint maximum();

    void setCount(uint count);
    bool setCurrent(uint current);
    bool setCurrent(subspace* current);

    void setRows(uint rows);
    void updateLayout();
    void setNavigationWrappingAround(bool enabled);

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

    /// Called when gesture ended, the thing that actually switches the desktop.
    QAction* swipe_gesture_released_y() const
    {
        return m_swipeGestureReleasedY.get();
    }
    QAction* swipe_gesture_released_x() const
    {
        return m_swipeGestureReleasedX.get();
    }
    QPointF m_current_desktop_offset() const
    {
        return m_currentDesktopOffset;
    }
    void set_desktop_offset_x(qreal offsetX)
    {
        m_currentDesktopOffset.setX(offsetX);
    }
    void set_desktop_offset_y(qreal offsetY)
    {
        m_currentDesktopOffset.setY(offsetY);
    }
    void connect_gestures();

    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement{nullptr};

private:
    void updateRootInfo();
    QList<subspace*> update_count(uint count);

    /// Generate a desktop layout from EWMH _NET_DESKTOP_LAYOUT property parameters.
    void
    setNETDesktopLayout(Qt::Orientation orientation, uint width, uint height, int startingCorner);

    QString defaultName(int desktop) const;

    QVector<subspace*> m_subspaces;
    QPointer<subspace> m_current;
    quint32 m_rows = 2;
    bool m_navigationWrapsAround{false};
    subspace_grid m_grid;
    // TODO: QPointer
    x11::net::root_info* m_rootInfo{nullptr};
    KSharedConfig::Ptr m_config;

    QScopedPointer<QAction> m_swipeGestureReleasedY;
    QScopedPointer<QAction> m_swipeGestureReleasedX;
    QPointF m_currentDesktopOffset = QPointF(0, 0);

    subspaces_singleton singleton;
};

inline uint subspace_manager::maximum()
{
    return 20;
}

inline uint subspace_manager::count() const
{
    return m_subspaces.count();
}

inline bool subspace_manager::isNavigationWrappingAround() const
{
    return m_navigationWrapsAround;
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
