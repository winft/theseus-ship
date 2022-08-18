/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "kwin_export.h"

#include <QAbstractItemModel>
#include <QList>
#include <QSortFilterProxyModel>

namespace KWin
{
class Client;

namespace scripting
{
class window;

namespace models::v2
{

class abstract_level;

class KWIN_EXPORT client_model : public QAbstractItemModel
{
    Q_OBJECT
    Q_ENUMS(Exclude)
    Q_PROPERTY(Exclusions exclusions READ exclusions WRITE setExclusions NOTIFY exclusionsChanged)
public:
    enum Exclusion {
        NoExclusion = 0,
        // window types
        DesktopWindowsExclusion = 1 << 0,
        DockWindowsExclusion = 1 << 1,
        UtilityWindowsExclusion = 1 << 2,
        SpecialWindowsExclusion = 1 << 3,
        // windows with flags
        SkipTaskbarExclusion = 1 << 4,
        SkipPagerExclusion = 1 << 5,
        SwitchSwitcherExclusion = 1 << 6,
        // based on state
        OtherDesktopsExclusion = 1 << 7,
        OtherActivitiesExclusion = 1 << 8,
        MinimizedExclusion = 1 << 9,
        NonSelectedWindowTabExclusion = 1 << 10,
        NotAcceptingFocusExclusion = 1 << 11
    };
    Q_DECLARE_FLAGS(Exclusions, Exclusion)
    Q_FLAGS(Exclusions)
    Q_ENUM(Exclusion)
    enum LevelRestriction {
        NoRestriction = 0,
        VirtualDesktopRestriction = 1 << 0,
        ScreenRestriction = 1 << 1,
        ActivityRestriction = 1 << 2
    };
    Q_DECLARE_FLAGS(LevelRestrictions, LevelRestriction)
    Q_FLAGS(LevelRestrictions)
    Q_ENUM(LevelRestriction)

    explicit client_model(QObject* parent);
    ~client_model() override;

    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QModelIndex
    index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setExclusions(client_model::Exclusions exclusions);
    Exclusions exclusions() const;

Q_SIGNALS:
    void exclusionsChanged();

private Q_SLOTS:
    void levelBeginInsert(int rowStart, int rowEnd, quint32 parentId);
    void levelEndInsert();
    void levelBeginRemove(int rowStart, int rowEnd, quint32 parentId);
    void levelEndRemove();

protected:
    enum ClientModelRoles {
        ClientRole = Qt::UserRole,
        ScreenRole,
        DesktopRole,
        ActivityRole,
    };
    void setLevels(QList<LevelRestriction> restrictions);

private:
    QModelIndex parentForId(quint32 childId) const;
    abstract_level const* getLevel(const QModelIndex& index) const;
    abstract_level* m_root;
    Exclusions m_exclusions;
};

/**
 * @brief The data structure of the Model.
 *
 * The model is implemented as a Tree consisting of abstract_levels as the levels of the tree.
 * A non leaf level is represented by the inheriting class fork_level, the last level above a
 * leaf is represented by the inheriting class client_level, which contains the Clients - each
 * Client is one leaf.
 *
 * In case the tree would only consist of Clients - leafs - it has always one client_level as the
 * root of the tree.
 *
 * The number of levels in the tree is controlled by the LevelRestrictions. For each existing
 * LevelRestriction a new Level is created, if there are no more restrictions a client_level is
 * created.
 *
 * To build up the tree the static factory method @ref create has to be used. It will recursively
 * build up the tree. After the tree has been build up use @ref init to initialize the tree which
 * will add the Clients to the client_level.
 *
 * Each element of the tree has a unique id which can be used by the QAbstractItemModel as the
 * internal id for its QModelIndex. Note: the ids have no ordering, if trying to get a specific
 * element the tree performs a depth-first search.
 */
class abstract_level : public QObject
{
    Q_OBJECT
public:
    ~abstract_level() override;

    virtual int count() const = 0;
    virtual void init() = 0;
    virtual quint32 idForRow(int row) const = 0;

    uint screen() const;
    uint virtualDesktop() const;
    QString const activity() const;
    client_model::LevelRestrictions restrictions() const;
    void setRestrictions(client_model::LevelRestrictions restrictions);
    client_model::LevelRestriction restriction() const;
    void setRestriction(client_model::LevelRestriction restriction);
    quint32 id() const;
    abstract_level* parentLevel() const;
    virtual const abstract_level* levelForId(quint32 id) const = 0;
    virtual abstract_level* parentForId(quint32 child) const = 0;
    virtual int rowForId(quint32 child) const = 0;
    virtual window* clientForId(quint32 child) const = 0;

    virtual void setScreen(uint screen);
    virtual void setVirtualDesktop(uint virtualDesktop);
    virtual void setActivity(const QString& activity);

    static abstract_level* create(const QList<client_model::LevelRestriction>& restrictions,
                                  client_model::LevelRestrictions parentRestrictions,
                                  client_model* model,
                                  abstract_level* parent = nullptr);

Q_SIGNALS:
    void beginInsert(int rowStart, int rowEnd, quint32 parentId);
    void endInsert();
    void beginRemove(int rowStart, int rowEnd, quint32 parentId);
    void endRemove();

protected:
    abstract_level(client_model* model, abstract_level* parent);
    client_model* model() const;

private:
    client_model* m_model;
    abstract_level* m_parent;
    uint m_screen;
    uint m_virtualDesktop;
    client_model::LevelRestriction m_restriction;
    client_model::LevelRestrictions m_restrictions;
    quint32 m_id;
};

class fork_level : public abstract_level
{
    Q_OBJECT
public:
    fork_level(const QList<client_model::LevelRestriction>& childRestrictions,
               client_model* model,
               abstract_level* parent);
    ~fork_level() override;
    int count() const override;
    void init() override;
    quint32 idForRow(int row) const override;
    void addChild(abstract_level* child);
    void setScreen(uint screen) override;
    void setVirtualDesktop(uint virtualDesktop) override;
    void setActivity(const QString& activity) override;
    const abstract_level* levelForId(quint32 id) const override;
    abstract_level* parentForId(quint32 child) const override;
    int rowForId(quint32 child) const override;
    window* clientForId(quint32 child) const override;
private Q_SLOTS:
    void desktopCountChanged(uint previousCount, uint newCount);
    void activityAdded(const QString& id);
    void activityRemoved(const QString& id);

private:
    void screenCountChanged(int previousCount, int newCount);

    QList<abstract_level*> m_children;
    QList<client_model::LevelRestriction> m_childRestrictions;
};

/**
 * @brief The actual leafs of the model's tree containing the Client's in this branch of the tree.
 *
 * This class groups all the Clients of one branch of the tree and takes care of updating the tree
 * when a Client changes its state in a way that it should be excluded/included or gets added or
 * removed.
 *
 * The Clients in this group are not sorted in any particular way. It's a simple list which only
 * gets added to. If some sorting should be applied, use a QSortFilterProxyModel.
 */
class KWIN_EXPORT client_level : public abstract_level
{
    Q_OBJECT
public:
    explicit client_level(client_model* model, abstract_level* parent);
    ~client_level() override;

    void init() override;

    int count() const override;
    quint32 idForRow(int row) const override;
    bool containsId(quint32 id) const;
    int rowForId(quint32 row) const override;
    window* clientForId(quint32 child) const override;
    const abstract_level* levelForId(quint32 id) const override;
    abstract_level* parentForId(quint32 child) const override;
public Q_SLOTS:
    void clientAdded(KWin::scripting::window* client);
    void clientRemoved(KWin::scripting::window* client);
private Q_SLOTS:
    // uses sender()
    void reInit();

private:
    void checkClient(window* client);
    void setupClientConnections(window* client);
    void addClient(window* client);
    void removeClient(window* client);
    bool shouldAdd(window* client) const;
    bool exclude(window* client) const;
    bool containsClient(window* client) const;
    QMap<quint32, window*> m_clients;
};

class KWIN_EXPORT simple_client_model : public client_model
{
    Q_OBJECT
public:
    simple_client_model(QObject* parent = nullptr);
    ~simple_client_model() override;
};

class KWIN_EXPORT client_model_by_screen : public client_model
{
    Q_OBJECT
public:
    client_model_by_screen(QObject* parent = nullptr);
    ~client_model_by_screen() override;
};

class KWIN_EXPORT client_model_by_screen_and_desktop : public client_model
{
    Q_OBJECT
public:
    client_model_by_screen_and_desktop(QObject* parent = nullptr);
    ~client_model_by_screen_and_desktop() override;
};

class KWIN_EXPORT client_model_by_screen_and_activity : public client_model
{
    Q_OBJECT
public:
    client_model_by_screen_and_activity(QObject* parent = nullptr);
    ~client_model_by_screen_and_activity() override;
};

/**
 * @brief Custom QSortFilterProxyModel to filter on Client caption, role and class.
 */
class KWIN_EXPORT client_filter_model : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(KWin::scripting::models::v2::client_model* clientModel READ clientModel WRITE
                   setClientModel NOTIFY clientModelChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
public:
    client_filter_model(QObject* parent = nullptr);
    ~client_filter_model() override;
    client_model* clientModel() const;
    const QString& filter() const;

public Q_SLOTS:
    void setClientModel(client_model* model);
    void setFilter(const QString& filter);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

Q_SIGNALS:
    void clientModelChanged();
    void filterChanged();

private:
    client_model* m_client_model;
    QString m_filter;
};

inline int client_level::count() const
{
    return m_clients.count();
}

inline QString const abstract_level::activity() const
{
    return {};
}

inline abstract_level* abstract_level::parentLevel() const
{
    return m_parent;
}

inline client_model* abstract_level::model() const
{
    return m_model;
}

inline uint abstract_level::screen() const
{
    return m_screen;
}

inline uint abstract_level::virtualDesktop() const
{
    return m_virtualDesktop;
}

inline client_model::LevelRestriction abstract_level::restriction() const
{
    return m_restriction;
}

inline client_model::LevelRestrictions abstract_level::restrictions() const
{
    return m_restrictions;
}

inline quint32 abstract_level::id() const
{
    return m_id;
}

inline client_model::Exclusions client_model::exclusions() const
{
    return m_exclusions;
}

inline client_model* client_filter_model::clientModel() const
{
    return m_client_model;
}

inline const QString& client_filter_model::filter() const
{
    return m_filter;
}

}
}
}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::scripting::models::v2::client_model::Exclusions)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::scripting::models::v2::client_model::LevelRestrictions)
