/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "kwinglobals.h"

#include <KConfig>
#include <KSharedConfig>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSize>

class KLocalizedString;
class NETRootInfo;
class QAction;

namespace Wrapland::Server
{
class PlasmaVirtualDesktopManager;
}

namespace KWin::win
{

class KWIN_EXPORT virtual_desktop : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QByteArray id READ id CONSTANT)
    Q_PROPERTY(uint x11DesktopNumber READ x11DesktopNumber NOTIFY x11DesktopNumberChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    explicit virtual_desktop(QObject* parent = nullptr);
    ~virtual_desktop() override;

    void setId(QByteArray const& id);
    QByteArray id() const
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
    QByteArray m_id;
    QString m_name;
    int m_x11DesktopNumber = 0;
};

/**
 * @brief Two dimensional grid containing the ID of the virtual desktop at a specific position
 * in the grid.
 *
 * The virtual_desktop_grid represents a visual layout of the Virtual Desktops as they are in e.g.
 * a Pager. This grid is used for getting a desktop next to a given desktop in any direction by
 * making use of the layout information. This allows navigation like move to desktop on left.
 */
class KWIN_EXPORT virtual_desktop_grid
{
public:
    virtual_desktop_grid();
    ~virtual_desktop_grid();

    void update(QSize const& size,
                Qt::Orientation orientation,
                QVector<virtual_desktop*> const& desktops);

    /**
     * @returns The coords of desktop @a id in grid units.
     */
    QPoint gridCoords(uint id) const;
    /**
     * @returns The coords of desktop @a vd in grid units.
     */
    QPoint gridCoords(virtual_desktop* vd) const;

    /**
     * @returns The desktop at the point @a coords or 0 if no desktop exists at that
     * point. @a coords is to be in grid units.
     */
    virtual_desktop* at(const QPoint& coords) const;
    int width() const;
    int height() const;
    QSize const& size() const;

private:
    QSize m_size;
    QVector<QVector<virtual_desktop*>> m_grid;
};

/**
 * @brief Manages the number of available virtual desktops, the layout of those and which virtual
 * desktop is the current one.
 *
 * This manager is responsible for Virtual Desktop handling inside KWin. It has a property for the
 * count of available virtual desktops and a property for the currently active virtual desktop. All
 * changes to the number of virtual desktops and the current virtual desktop need to go through this
 * manager.
 *
 * On all changes a signal is emitted and interested parties should connect to the signal. The
 * manager itself does not interact with other parts of the system. E.g. it does not hide/show
 * windows of desktop changes. This is outside the scope of this manager.
 *
 * Internally the manager organizes the virtual desktops in a grid allowing to navigate over the
 * virtual desktops. For this a set of convenient methods are available which allow to get the id
 * of an adjacent desktop or to switch to an adjacent desktop. Interested parties should make use of
 * these methods and not replicate the logic to switch to the next desktop.
 */
class KWIN_EXPORT virtual_desktop_manager : public QObject
{
    Q_OBJECT

    /**
     * The number of virtual desktops currently available.
     * The ids of the virtual desktops are in the range [1, virtual_desktop_manager::maximum()].
     */
    Q_PROPERTY(uint count READ count WRITE setCount NOTIFY countChanged)
    /**
     * The id of the virtual desktop which is currently in use.
     */
    Q_PROPERTY(uint current READ current WRITE setCurrent NOTIFY currentChanged)
    /**
     * Whether navigation in the desktop layout wraps around at the borders.
     */
    Q_PROPERTY(bool navigationWrappingAround READ isNavigationWrappingAround WRITE
                   setNavigationWrappingAround NOTIFY navigationWrappingAroundChanged)

public:
    ~virtual_desktop_manager() override;
    /**
     * @internal, for X11 case
     */
    void setRootInfo(NETRootInfo* info);
    /**
     * @internal, for Wayland case
     */
    void setVirtualDesktopManagement(Wrapland::Server::PlasmaVirtualDesktopManager* management);
    /**
     * @internal
     */
    void setConfig(KSharedConfig::Ptr config);
    /**
     * @returns Total number of desktops currently in existence.
     * @see setCount
     * @see countChanged
     */
    uint count() const;
    /**
     * @returns the number of rows the layout has.
     * @see setRows
     * @see rowsChanged
     */
    uint rows() const;
    /**
     * @returns The ID of the current desktop.
     * @see setCurrent
     * @see currentChanged
     */
    uint current() const;
    /**
     * @returns The current desktop
     * @see setCurrent
     * @see currentChanged
     */
    virtual_desktop* currentDesktop() const;
    /**
     * Moves to the desktop through the algorithm described by Direction.
     * @param wrap If @c true wraps around to the other side of the layout
     * @see setCurrent
     */
    template<typename Direction>
    void moveTo(bool wrap = false);

    /**
     * @returns The name of the @p desktop
     */
    QString name(uint desktop) const;

    /**
     * @returns @c true if navigation at borders of layout wraps around, @c false otherwise
     * @see setNavigationWrappingAround
     * @see navigationWrappingAroundChanged
     */
    bool isNavigationWrappingAround() const;

    /**
     * @returns The layout aware virtual desktop grid used by this manager.
     */
    const virtual_desktop_grid& grid() const;

    /**
     * @returns The ID of the desktop above desktop @a id. Wraps around to the bottom of
     * the layout if @a wrap is set. If @a id is not set use the current one.
     */
    uint above(uint id = 0, bool wrap = true) const;
    /**
     * @returns The desktop above desktop @a desktop. Wraps around to the bottom of
     * the layout if @a wrap is set. If @a desktop is @c null use the current one.
     */
    virtual_desktop* above(virtual_desktop* desktop, bool wrap = true) const;
    /**
     * @returns The ID of the desktop to the right of desktop @a id. Wraps around to the
     * left of the layout if @a wrap is set. If @a id is not set use the current one.
     */
    uint toRight(uint id = 0, bool wrap = true) const;
    /**
     * @returns The desktop to the right of desktop @a desktop. Wraps around to the
     * left of the layout if @a wrap is set. If @a desktop is @c null use the current one.
     */
    virtual_desktop* toRight(virtual_desktop* desktop, bool wrap = true) const;
    /**
     * @returns The ID of the desktop below desktop @a id. Wraps around to the top of the
     * layout if @a wrap is set. If @a id is not set use the current one.
     */
    uint below(uint id = 0, bool wrap = true) const;
    /**
     * @returns The desktop below desktop @a desktop. Wraps around to the top of the
     * layout if @a wrap is set. If @a desktop is @c null use the current one.
     */
    virtual_desktop* below(virtual_desktop* desktop, bool wrap = true) const;
    /**
     * @returns The ID of the desktop to the left of desktop @a id. Wraps around to the
     * right of the layout if @a wrap is set. If @a id is not set use the current one.
     */
    uint toLeft(uint id = 0, bool wrap = true) const;
    /**
     * @returns The desktop to the left of desktop @a desktop. Wraps around to the
     * right of the layout if @a wrap is set. If @a desktop is @c null use the current one.
     */
    virtual_desktop* toLeft(virtual_desktop* desktop, bool wrap = true) const;
    /**
     * @returns The desktop after the desktop @a desktop. Wraps around to the first
     * desktop if @a wrap is set. If @a desktop is @c null use the current desktop.
     */
    virtual_desktop* next(virtual_desktop* desktop = nullptr, bool wrap = true) const;
    /**
     * @returns The desktop in front of the desktop @a desktop. Wraps around to the
     * last desktop if @a wrap is set. If @a desktop is @c null use the current desktop.
     */
    virtual_desktop* previous(virtual_desktop* desktop = nullptr, bool wrap = true) const;

    void initShortcuts();

    /**
     * @returns all currently managed virtual_desktops
     */
    QVector<virtual_desktop*> desktops() const
    {
        return m_desktops;
    }

    /**
     * @returns The virtual_desktop for the x11 @p id, if no such virtual_desktop @c null is
     * returned
     */
    virtual_desktop* desktopForX11Id(uint id) const;

    /**
     * @returns The virtual_desktop for the internal desktop string @p id, if no such
     * virtual_desktop
     * @c null is returned
     */
    virtual_desktop* desktopForId(QByteArray const& id) const;

    /**
     * Create a new virtual desktop at the requested position.
     * The difference with setCount is that setCount always adds new desktops at the end of the
     * chain. The Id is automatically generated.
     * @param position The position of the desktop. It should be in range [0, count].
     * @param name The name for the new desktop, if empty the default name will be used.
     * @returns the new virtual_desktop, nullptr if we reached the maximum number of desktops
     */
    virtual_desktop* createVirtualDesktop(uint position, QString const& name = QString());

    /**
     * Remove the virtual desktop identified by id, if it exists
     * difference with setCount is that is possible to remove an arbitrary desktop,
     * not only the last one.
     * @param id the string id of the desktop to remove
     */
    void removeVirtualDesktop(QByteArray const& id);

    /**
     * Updates the net root info for new number of desktops
     */
    void updateRootInfo();

    /**
     * @returns The maximum number of desktops that KWin supports.
     */
    static uint maximum();

public Q_SLOTS:
    /**
     * Set the number of available desktops to @a count. This function overrides any previous
     * grid layout.
     * There needs to be at least one virtual desktop and the new value is capped at the maximum
     * number of desktops. A caller of this function cannot expect that the change has been applied.
     * It is the callers responsibility to either check the numberOfDesktops or connect to the
     * countChanged signal.
     *
     * In case the @ref current desktop is on a desktop higher than the new count, the current
     * desktop is changed to be the new desktop with highest id. In that situation the signal
     * desktopRemoved is emitted.
     * @param count The new number of desktops to use
     * @see count
     * @see maximum
     * @see countChanged
     * @see desktopCreated
     * @see desktopRemoved
     */
    void setCount(uint count);
    /**
     * Set the current desktop to @a current.
     * @returns True on success, false otherwise.
     * @see current
     * @see currentChanged
     * @see moveTo
     */
    bool setCurrent(uint current);
    /**
     * Set the current desktop to @a current.
     * @returns True on success, false otherwise.
     * @see current
     * @see currentChanged
     * @see moveTo
     */
    bool setCurrent(virtual_desktop* current);
    /**
     * Updates the layout to a new number of rows. The number of columns will be calculated
     * accordingly
     */
    void setRows(uint rows);
    /**
     * Called from within setCount() to ensure the desktop layout is still valid.
     */
    void updateLayout();
    /**
     * @param enabled wrapping around borders for navigation in desktop layout
     * @see isNavigationWrappingAround
     * @see navigationWrappingAroundChanged
     */
    void setNavigationWrappingAround(bool enabled);
    /**
     * Loads number of desktops and names from configuration file
     */
    void load();
    /**
     * Saves number of desktops and names to configuration file
     */
    void save();

Q_SIGNALS:
    /**
     * Signal emitted whenever the number of virtual desktops changes.
     * @param previousCount The number of desktops prior to the change
     * @param newCount The new current number of desktops
     */
    void countChanged(uint previousCount, uint newCount);

    /**
     * Signal when the number of rows in the layout changes
     * @param rows number of rows
     */
    void rowsChanged(uint rows);

    /**
     * A new desktop has been created
     * @param desktop the new just crated desktop
     */
    void desktopCreated(KWin::win::virtual_desktop* desktop);

    /**
     * A desktop has been removed and is about to be deleted
     * @param desktop the desktop that has been removed.
     *          It's guaranteed to stil la valid pointer when the signal arrives,
     *          but it's about to be deleted.
     */
    void desktopRemoved(KWin::win::virtual_desktop* desktop);

    /**
     * Signal emitted whenever the current desktop changes.
     * @param previousDesktop The virtual desktop changed from
     * @param newDesktop The virtual desktop changed to
     */
    void currentChanged(uint previousDesktop, uint newDesktop);
    /**
     * Signal emitted whenever the desktop layout changes.
     * @param columns The new number of columns in the layout
     * @param rows The new number of rows in the layout
     */
    void layoutChanged(int columns, int rows);
    /**
     * Signal emitted whenever the navigationWrappingAround property changes.
     */
    void navigationWrappingAroundChanged();

private Q_SLOTS:
    /**
     * Common slot for all "Switch to Desktop n" shortcuts.
     * This method uses the sender() method to access some data.
     * DO NOT CALL DIRECTLY! ONLY TO BE USED FROM AN ACTION!
     */
    void slotSwitchTo();
    /**
     * Slot for switch to next desktop action.
     */
    void slotNext();
    /**
     * Slot for switch to previous desktop action.
     */
    void slotPrevious();
    /**
     * Slot for switch to right desktop action.
     */
    void slotRight();
    /**
     * Slot for switch to left desktop action.
     */
    void slotLeft();
    /**
     * Slot for switch to desktop above action.
     */
    void slotUp();
    /**
     * Slot for switch to desktop below action.
     */
    void slotDown();

private:
    /**
     * Generate a desktop layout from EWMH _NET_DESKTOP_LAYOUT property parameters.
     */
    void
    setNETDesktopLayout(Qt::Orientation orientation, uint width, uint height, int startingCorner);
    /**
     * @returns A default name for the given @p desktop
     */
    QString defaultName(int desktop) const;
    /**
     * Creates all the global keyboard shortcuts for "Switch To Desktop n" actions.
     */
    void initSwitchToShortcuts();
    /**
     * Creates an action and connects it to the @p slot in this Manager. This method is
     * meant to be used for the case that an additional information needs to be stored in
     * the action and the label.
     * @param name The name of the action to be created
     * @param label The localized name for the action to be created
     * @param value An additional value added to the label and to the created action
     * @param key The global shortcut for the action
     * @param slot The slot to invoke when the action is triggered
     */
    QAction* addAction(QString const& name,
                       KLocalizedString const& label,
                       uint value,
                       QKeySequence const& key,
                       void (virtual_desktop_manager::*slot)());
    /**
     * Creates an action and connects it to the @p slot in this Manager.
     * Overloaded method for the case that no additional value needs to be passed to the action and
     * no global shortcut is defined by default.
     * @param name The name of the action to be created
     * @param label The localized name for the action to be created
     * @param slot The slot to invoke when the action is triggered
     */
    QAction*
    addAction(QString const& name, QString const& label, void (virtual_desktop_manager::*slot)());

    QVector<virtual_desktop*> m_desktops;
    QPointer<virtual_desktop> m_current;
    quint32 m_rows = 2;
    bool m_navigationWrapsAround;
    virtual_desktop_grid m_grid;
    // TODO: QPointer
    NETRootInfo* m_rootInfo;
    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement = nullptr;
    KSharedConfig::Ptr m_config;

    KWIN_SINGLETON_VARIABLE(virtual_desktop_manager, s_manager)
};

/**
 * Function object to select the desktop above in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_above
{
public:
    virtual_desktop_above()
    {
    }
    /**
     * @param desktop The desktop from which the desktop above should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already topmost desktop
     * @returns Id of the desktop above @p desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the desktop above should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already topmost desktop
     * @returns the desktop above @p desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->above(desktop, wrap);
    }
};

/**
 * Function object to select the desktop below in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_below
{
public:
    virtual_desktop_below()
    {
    }
    /**
     * @param desktop The desktop from which the desktop below should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already lowest desktop
     * @returns Id of the desktop below @p desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the desktop below should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already lowest desktop
     * @returns the desktop below @p desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->below(desktop, wrap);
    }
};

/**
 * Function object to select the desktop to the left in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_left
{
public:
    virtual_desktop_left()
    {
    }
    /**
     * @param desktop The desktop from which the desktop on the left should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already leftmost desktop
     * @returns Id of the desktop left of @p desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the desktop on the left should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already leftmost desktop
     * @returns the desktop left of @p desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->toLeft(desktop, wrap);
    }
};

/**
 * Function object to select the desktop to the right in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_right
{
public:
    virtual_desktop_right()
    {
    }
    /**
     * @param desktop The desktop from which the desktop on the right should be selected. If @c 0
     * the current desktop is used
     * @param wrap Whether to wrap around if already rightmost desktop
     * @returns Id of the desktop right of @p desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the desktop on the right should be selected. If @c 0
     * the current desktop is used
     * @param wrap Whether to wrap around if already rightmost desktop
     * @returns the desktop right of @p desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->toRight(desktop, wrap);
    }
};

/**
 * Function object to select the next desktop in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_next
{
public:
    virtual_desktop_next()
    {
    }
    /**
     * @param desktop The desktop from which the next desktop should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already last desktop
     * @returns Id of the next desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the next desktop should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already last desktop
     * @returns the next desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->next(desktop, wrap);
    }
};

/**
 * Function object to select the previous desktop in the layout.
 * Note: does not switch to the desktop!
 */
class virtual_desktop_previous
{
public:
    virtual_desktop_previous()
    {
    }
    /**
     * @param desktop The desktop from which the previous desktop should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already first desktop
     * @returns Id of the previous desktop
     */
    uint operator()(uint desktop, bool wrap)
    {
        return (*this)(virtual_desktop_manager::self()->desktopForX11Id(desktop), wrap)
            ->x11DesktopNumber();
    }
    /**
     * @param desktop The desktop from which the previous desktop should be selected. If @c 0 the
     * current desktop is used
     * @param wrap Whether to wrap around if already first desktop
     * @returns the previous desktop
     */
    virtual_desktop* operator()(virtual_desktop* desktop, bool wrap)
    {
        return virtual_desktop_manager::self()->previous(desktop, wrap);
    }
};

/**
 * Helper function to get the ID of a virtual desktop in the direction from
 * the given @p desktop. If @c 0 the current desktop is used as a starting point.
 * @param desktop The desktop from which the desktop in given Direction should be selected.
 * @param wrap Whether desktop navigation wraps around at the borders of the layout
 * @returns The next desktop in specified direction
 */
template<typename Direction>
uint getDesktop(int desktop = 0, bool wrap = true);

template<typename Direction>
uint getDesktop(int d, bool wrap)
{
    Direction direction;
    return direction(d, wrap);
}

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

template<typename Direction>
void virtual_desktop_manager::moveTo(bool wrap)
{
    Direction functor;
    setCurrent(functor(nullptr, wrap));
}

}
