/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "singleton_interface.h"

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

class KWIN_EXPORT virtual_desktop : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(uint x11DesktopNumber READ x11DesktopNumber NOTIFY x11DesktopNumberChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    explicit virtual_desktop(QObject* parent = nullptr);
    ~virtual_desktop() override;

    void setId(QString const& id);
    QString id() const
    {
        return m_id;
    }

    void setName(QString const& name);
    QString name() const
    {
        return m_name;
    }

    void setX11DesktopNumber(uint number);
    uint x11DesktopNumber() const
    {
        return m_x11DesktopNumber;
    }

Q_SIGNALS:
    void nameChanged();
    void x11DesktopNumberChanged();
    /**
     * Emitted just before the desktop gets destroyed.
     */
    void aboutToBeDestroyed();

private:
    QString m_id;
    QString m_name;
    int m_x11DesktopNumber = 0;
};

class virtual_desktop_manager;

class KWIN_EXPORT virtual_desktop_grid
{
public:
    virtual_desktop_grid(virtual_desktop_manager& manager);
    ~virtual_desktop_grid();

    void update(QSize const& size,
                Qt::Orientation orientation,
                QVector<virtual_desktop*> const& desktops);

    QPoint gridCoords(uint id) const;
    QPoint gridCoords(virtual_desktop* vd) const;

    virtual_desktop* at(const QPoint& coords) const;
    int width() const;
    int height() const;
    QSize const& size() const;

private:
    QSize m_size;
    QVector<QVector<virtual_desktop*>> m_grid;
    virtual_desktop_manager& manager;
};

class KWIN_EXPORT virtual_desktop_manager_qobject : public QObject
{
    Q_OBJECT
public:
    virtual_desktop_manager_qobject();

Q_SIGNALS:
    void countChanged(uint previousCount, uint newCount);
    void rowsChanged(uint rows);

    void desktopCreated(KWin::win::virtual_desktop* desktop);
    void desktopRemoved(KWin::win::virtual_desktop* desktop);

    void currentChanged(uint previousDesktop, uint newDesktop);

    /**
     * For realtime desktop switching animations. Offset is current total change in desktop
     * coordinate. x and y are negative if switching left/down. Example: x = 0.6 means 60% of the
     * way to the desktop to the right.
     */
    void currentChanging(uint currentDesktop, QPointF offset);
    void currentChangingCancelled();

    void layoutChanged(int columns, int rows);
    void navigationWrappingAroundChanged();
};

class KWIN_EXPORT virtual_desktop_manager
{
public:
    virtual_desktop_manager();
    ~virtual_desktop_manager();

    void setRootInfo(x11::net::root_info* info);
    void setConfig(KSharedConfig::Ptr config);

    uint count() const;
    uint rows() const;

    uint current() const;
    virtual_desktop* currentDesktop() const;

    QString name(uint desktop) const;
    bool isNavigationWrappingAround() const;
    const virtual_desktop_grid& grid() const;

    uint above(uint id = 0, bool wrap = true) const;
    virtual_desktop* above(virtual_desktop* desktop, bool wrap = true) const;

    uint toRight(uint id = 0, bool wrap = true) const;
    virtual_desktop* toRight(virtual_desktop* desktop, bool wrap = true) const;

    uint below(uint id = 0, bool wrap = true) const;
    virtual_desktop* below(virtual_desktop* desktop, bool wrap = true) const;

    uint toLeft(uint id = 0, bool wrap = true) const;
    virtual_desktop* toLeft(virtual_desktop* desktop, bool wrap = true) const;

    virtual_desktop* next(virtual_desktop* desktop = nullptr, bool wrap = true) const;
    uint next(uint id = 0, bool wrap = true) const;

    virtual_desktop* previous(virtual_desktop* desktop = nullptr, bool wrap = true) const;
    uint previous(uint id = 0, bool wrap = true) const;

    QVector<virtual_desktop*> desktops() const
    {
        return m_desktops;
    }

    virtual_desktop* desktopForX11Id(uint id) const;
    virtual_desktop* desktopForId(QString const& id) const;

    /**
     * Create a new virtual desktop at the requested position. The difference with setCount is that
     * setCount always adds new desktops at the end of the chain. The Id is automatically generated.
     * @returns the new virtual_desktop, nullptr if we reached the maximum number of desktops.
     */
    virtual_desktop* createVirtualDesktop(uint position, QString const& name = QString());

    void removeVirtualDesktop(QString const& id);
    void removeVirtualDesktop(virtual_desktop* desktop);

    void updateRootInfo();
    static uint maximum();

    void setCount(uint count);
    bool setCurrent(uint current);
    bool setCurrent(virtual_desktop* current);

    void setRows(uint rows);
    void updateLayout();
    void setNavigationWrappingAround(bool enabled);

    void load();
    void save();

    std::unique_ptr<virtual_desktop_manager_qobject> qobject;

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
    QList<virtual_desktop*> update_count(uint count);

    /// Generate a desktop layout from EWMH _NET_DESKTOP_LAYOUT property parameters.
    void
    setNETDesktopLayout(Qt::Orientation orientation, uint width, uint height, int startingCorner);

    QString defaultName(int desktop) const;

    QVector<virtual_desktop*> m_desktops;
    QPointer<virtual_desktop> m_current;
    quint32 m_rows = 2;
    bool m_navigationWrapsAround{false};
    virtual_desktop_grid m_grid;
    // TODO: QPointer
    x11::net::root_info* m_rootInfo{nullptr};
    KSharedConfig::Ptr m_config;

    QScopedPointer<QAction> m_swipeGestureReleasedY;
    QScopedPointer<QAction> m_swipeGestureReleasedX;
    QPointF m_currentDesktopOffset = QPointF(0, 0);

    virtual_desktops_singleton singleton;
};

inline int virtual_desktop_grid::width() const
{
    return m_size.width();
}

inline int virtual_desktop_grid::height() const
{
    return m_size.height();
}

inline QSize const& virtual_desktop_grid::size() const
{
    return m_size;
}

inline uint virtual_desktop_manager::maximum()
{
    return 20;
}

inline uint virtual_desktop_manager::count() const
{
    return m_desktops.count();
}

inline bool virtual_desktop_manager::isNavigationWrappingAround() const
{
    return m_navigationWrapsAround;
}

inline void virtual_desktop_manager::setConfig(KSharedConfig::Ptr config)
{
    m_config = std::move(config);
}

inline virtual_desktop_grid const& virtual_desktop_manager::grid() const
{
    return m_grid;
}

}
