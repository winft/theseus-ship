/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "client_model.h"

#include "scripting/platform.h"
#include "scripting/space.h"
#include "scripting/window.h"

#include "base/wayland/server.h"
#include "screens.h"
#include "toplevel.h"
#include "win/virtual_desktops.h"
#include "workspace.h"
#include <config-kwin.h>

#include "win/meta.h"
#include "win/net.h"

namespace KWin::scripting::models::v2
{

static quint32 nextId()
{
    static quint32 counter = 0;
    return ++counter;
}

client_level::client_level(client_model* model, abstract_level* parent)
    : abstract_level(model, parent)
{
    auto ws_wrap = workspace()->scripting->workspaceWrapper();

    connect(win::virtual_desktop_manager::self(),
            &win::virtual_desktop_manager::currentChanged,
            this,
            &client_level::reInit);
    connect(ws_wrap, &space::clientAdded, this, &client_level::clientAdded);
    connect(ws_wrap, &space::clientRemoved, this, &client_level::clientRemoved);
    connect(model, &client_model::exclusionsChanged, this, &client_level::reInit);
}

client_level::~client_level()
{
}

void client_level::clientAdded(window* client)
{
    setupClientConnections(client);
    checkClient(client);
}

void client_level::clientRemoved(window* client)
{
    removeClient(client);
}

void client_level::setupClientConnections(window* client)
{
    auto check = [this, client] { checkClient(client); };
    connect(client, &window::desktopChanged, this, check);
    connect(client, &window::screenChanged, this, check);
    connect(client->client(), &Toplevel::windowHidden, this, check);
    connect(client->client(), &Toplevel::windowShown, this, check);
}

void client_level::checkClient(window* client)
{
    const bool shouldInclude = !exclude(client) && shouldAdd(client);
    const bool contains = containsClient(client);

    if (shouldInclude && !contains) {
        addClient(client);
    } else if (!shouldInclude && contains) {
        removeClient(client);
    }
}

bool client_level::exclude(window* client) const
{
    client_model::Exclusions exclusions = model()->exclusions();
    if (exclusions == client_model::NoExclusion) {
        return false;
    }
    if (exclusions & client_model::DesktopWindowsExclusion) {
        if (win::is_desktop(client)) {
            return true;
        }
    }
    if (exclusions & client_model::DockWindowsExclusion) {
        if (win::is_dock(client)) {
            return true;
        }
    }
    if (exclusions & client_model::UtilityWindowsExclusion) {
        if (win::is_utility(client)) {
            return true;
        }
    }
    if (exclusions & client_model::SpecialWindowsExclusion) {
        if (win::is_special_window(client)) {
            return true;
        }
    }
    if (exclusions & client_model::SkipTaskbarExclusion) {
        if (client->skipTaskbar()) {
            return true;
        }
    }
    if (exclusions & client_model::SkipPagerExclusion) {
        if (client->skipPager()) {
            return true;
        }
    }
    if (exclusions & client_model::SwitchSwitcherExclusion) {
        if (client->skipSwitcher()) {
            return true;
        }
    }
    if (exclusions & client_model::OtherDesktopsExclusion) {
        if (!client->client()->isOnCurrentDesktop()) {
            return true;
        }
    }
    if (exclusions & client_model::MinimizedExclusion) {
        if (client->isMinimized()) {
            return true;
        }
    }
    if (exclusions & client_model::NotAcceptingFocusExclusion) {
        if (!client->wantsInput()) {
            return true;
        }
    }
    return false;
}

bool client_level::shouldAdd(window* client) const
{
    if (restrictions() == client_model::NoRestriction) {
        return true;
    }
    if (restrictions() & client_model::VirtualDesktopRestriction) {
        if (!client->client()->isOnDesktop(virtualDesktop())) {
            return false;
        }
    }
    if (restrictions() & client_model::ScreenRestriction) {
        if (client->screen() != int(screen())) {
            return false;
        }
    }
    return true;
}

void client_level::addClient(window* client)
{
    if (containsClient(client)) {
        return;
    }
    Q_EMIT beginInsert(m_clients.count(), m_clients.count(), id());
    m_clients.insert(nextId(), client);
    Q_EMIT endInsert();
}

void client_level::removeClient(window* client)
{
    int index = 0;
    auto it = m_clients.begin();
    for (; it != m_clients.end(); ++it, ++index) {
        if (it.value() == client) {
            break;
        }
    }
    if (it == m_clients.end()) {
        return;
    }
    Q_EMIT beginRemove(index, index, id());
    m_clients.erase(it);
    Q_EMIT endRemove();
}

void client_level::init()
{
    auto const& clients = workspace()->scripting->workspaceWrapper()->clientList();
    for (auto const& client : clients) {
        setupClientConnections(client);
        if (!exclude(client) && shouldAdd(client)) {
            m_clients.insert(nextId(), client);
        }
    }
}

void client_level::reInit()
{
    auto const& clients = workspace()->scripting->workspaceWrapper()->clientList();
    for (auto const& client : clients) {
        checkClient(client);
    }
}

quint32 client_level::idForRow(int row) const
{
    if (row >= m_clients.size()) {
        return 0;
    }
    auto it = m_clients.constBegin();
    for (int i = 0; i < row; ++i) {
        ++it;
    }
    return it.key();
}

bool client_level::containsId(quint32 id) const
{
    return m_clients.contains(id);
}

int client_level::rowForId(quint32 id) const
{
    int row = 0;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it, ++row) {
        if (it.key() == id) {
            return row;
        }
    }
    return -1;
}

window* client_level::clientForId(quint32 child) const
{
    auto it = m_clients.constFind(child);
    if (it == m_clients.constEnd()) {
        return nullptr;
    }
    return it.value();
}

bool client_level::containsClient(window* client) const
{
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        if (it.value() == client) {
            return true;
        }
    }
    return false;
}

const abstract_level* client_level::levelForId(quint32 id) const
{
    if (id == abstract_level::id()) {
        return this;
    }
    return nullptr;
}

abstract_level* client_level::parentForId(quint32 child) const
{
    if (child == id()) {
        return parentLevel();
    }
    if (m_clients.contains(child)) {
        return const_cast<client_level*>(this);
    }
    return nullptr;
}

abstract_level* abstract_level::create(const QList<client_model::LevelRestriction>& restrictions,
                                       client_model::LevelRestrictions parentRestrictions,
                                       client_model* model,
                                       abstract_level* parent)
{
    if (restrictions.isEmpty() || restrictions.first() == client_model::NoRestriction) {
        auto leaf = new client_level(model, parent);
        leaf->setRestrictions(parentRestrictions);
        if (!parent) {
            leaf->setParent(model);
        }
        return leaf;
    }
    // create a level
    QList<client_model::LevelRestriction> childRestrictions(restrictions);
    client_model::LevelRestriction restriction = childRestrictions.takeFirst();
    client_model::LevelRestrictions childrenRestrictions = restriction | parentRestrictions;
    auto currentLevel = new fork_level(childRestrictions, model, parent);
    currentLevel->setRestrictions(childrenRestrictions);
    currentLevel->setRestriction(restriction);
    if (!parent) {
        currentLevel->setParent(model);
    }
    switch (restriction) {
    case client_model::ActivityRestriction: {
        return nullptr;
    }
    case client_model::ScreenRestriction: {
        auto screen_count = kwinApp()->get_base().screens.count();
        for (int i = 0; i < screen_count; ++i) {
            auto childLevel = create(childRestrictions, childrenRestrictions, model, currentLevel);
            if (!childLevel) {
                continue;
            }
            childLevel->setScreen(i);
            currentLevel->addChild(childLevel);
        }
        break;
    }
    case client_model::VirtualDesktopRestriction:
        for (uint i = 1; i <= win::virtual_desktop_manager::self()->count(); ++i) {
            auto childLevel = create(childRestrictions, childrenRestrictions, model, currentLevel);
            if (!childLevel) {
                continue;
            }
            childLevel->setVirtualDesktop(i);
            currentLevel->addChild(childLevel);
        }
        break;
    default:
        // invalid
        return nullptr;
    }

    return currentLevel;
}

abstract_level::abstract_level(client_model* model, abstract_level* parent)
    : QObject(parent)
    , m_model(model)
    , m_parent(parent)
    , m_screen(0)
    , m_virtualDesktop(0)
    , m_restriction(client_model::client_model::NoRestriction)
    , m_restrictions(client_model::NoRestriction)
    , m_id(nextId())
{
}

abstract_level::~abstract_level()
{
}

void abstract_level::setRestriction(client_model::LevelRestriction restriction)
{
    m_restriction = restriction;
}

// TODO(romangg): Can activity-related functions be safely removed?
void abstract_level::setActivity(const QString& /*activity*/)
{
}

void abstract_level::setScreen(uint screen)
{
    m_screen = screen;
}

void abstract_level::setVirtualDesktop(uint virtualDesktop)
{
    m_virtualDesktop = virtualDesktop;
}

void abstract_level::setRestrictions(client_model::LevelRestrictions restrictions)
{
    m_restrictions = restrictions;
}

fork_level::fork_level(const QList<client_model::LevelRestriction>& childRestrictions,
                       client_model* model,
                       abstract_level* parent)
    : abstract_level(model, parent)
    , m_childRestrictions(childRestrictions)
{
    connect(win::virtual_desktop_manager::self(),
            &win::virtual_desktop_manager::countChanged,
            this,
            &fork_level::desktopCountChanged);
    connect(&kwinApp()->get_base().screens,
            &Screens::countChanged,
            this,
            &fork_level::screenCountChanged);
}

fork_level::~fork_level()
{
}

void fork_level::desktopCountChanged(uint previousCount, uint newCount)
{
    if (restriction() != client_model::client_model::VirtualDesktopRestriction) {
        return;
    }
    if (previousCount != uint(count())) {
        return;
    }
    if (previousCount > newCount) {
        // desktops got removed
        Q_EMIT beginRemove(newCount, previousCount - 1, id());
        while (uint(m_children.count()) > newCount) {
            delete m_children.takeLast();
        }
        Q_EMIT endRemove();
    } else {
        // desktops got added
        Q_EMIT beginInsert(previousCount, newCount - 1, id());
        for (uint i = previousCount + 1; i <= newCount; ++i) {
            abstract_level* childLevel
                = abstract_level::create(m_childRestrictions, restrictions(), model(), this);
            if (!childLevel) {
                continue;
            }
            childLevel->setVirtualDesktop(i);
            childLevel->init();
            addChild(childLevel);
        }
        Q_EMIT endInsert();
    }
}

void fork_level::screenCountChanged(int previousCount, int newCount)
{
    if (restriction() != client_model::client_model::client_model::ScreenRestriction) {
        return;
    }
    if (newCount == previousCount || previousCount != count()) {
        return;
    }

    if (previousCount > newCount) {
        // screens got removed
        Q_EMIT beginRemove(newCount, previousCount - 1, id());
        while (m_children.count() > newCount) {
            delete m_children.takeLast();
        }
        Q_EMIT endRemove();
    } else {
        // screens got added
        Q_EMIT beginInsert(previousCount, newCount - 1, id());
        for (int i = previousCount; i < newCount; ++i) {
            auto childLevel
                = abstract_level::create(m_childRestrictions, restrictions(), model(), this);
            if (!childLevel) {
                continue;
            }
            childLevel->setScreen(i);
            childLevel->init();
            addChild(childLevel);
        }
        Q_EMIT endInsert();
    }
}

void fork_level::activityAdded(const QString& /*activityId*/)
{
}

void fork_level::activityRemoved(const QString& /*activityId*/)
{
}

int fork_level::count() const
{
    return m_children.count();
}

void fork_level::addChild(abstract_level* child)
{
    m_children.append(child);
    connect(child, &abstract_level::beginInsert, this, &abstract_level::beginInsert);
    connect(child, &abstract_level::beginRemove, this, &abstract_level::beginRemove);
    connect(child, &abstract_level::endInsert, this, &abstract_level::endInsert);
    connect(child, &abstract_level::endRemove, this, &abstract_level::endRemove);
}

void fork_level::setActivity(const QString& /*activity*/)
{
}

void fork_level::setScreen(uint screen)
{
    abstract_level::setScreen(screen);
    for (QList<abstract_level*>::iterator it = m_children.begin(); it != m_children.end(); ++it) {
        (*it)->setScreen(screen);
    }
}

void fork_level::setVirtualDesktop(uint virtualDesktop)
{
    abstract_level::setVirtualDesktop(virtualDesktop);
    for (QList<abstract_level*>::iterator it = m_children.begin(); it != m_children.end(); ++it) {
        (*it)->setVirtualDesktop(virtualDesktop);
    }
}

void fork_level::init()
{
    for (QList<abstract_level*>::iterator it = m_children.begin(); it != m_children.end(); ++it) {
        (*it)->init();
    }
}

quint32 fork_level::idForRow(int row) const
{
    if (row >= m_children.length()) {
        return 0;
    }
    return m_children.at(row)->id();
}

const abstract_level* fork_level::levelForId(quint32 id) const
{
    if (id == abstract_level::id()) {
        return this;
    }
    for (QList<abstract_level*>::const_iterator it = m_children.constBegin();
         it != m_children.constEnd();
         ++it) {
        if (const abstract_level* child = (*it)->levelForId(id)) {
            return child;
        }
    }
    // not found
    return nullptr;
}

abstract_level* fork_level::parentForId(quint32 child) const
{
    if (child == id()) {
        return parentLevel();
    }
    for (QList<abstract_level*>::const_iterator it = m_children.constBegin();
         it != m_children.constEnd();
         ++it) {
        if (auto parent = (*it)->parentForId(child)) {
            return parent;
        }
    }
    // not found
    return nullptr;
}

int fork_level::rowForId(quint32 child) const
{
    if (id() == child) {
        return 0;
    }
    for (int i = 0; i < m_children.count(); ++i) {
        if (m_children.at(i)->id() == child) {
            return i;
        }
    }
    // do recursion
    for (QList<abstract_level*>::const_iterator it = m_children.constBegin();
         it != m_children.constEnd();
         ++it) {
        int row = (*it)->rowForId(child);
        if (row != -1) {
            return row;
        }
    }
    // not found
    return -1;
}

window* fork_level::clientForId(quint32 child) const
{
    for (QList<abstract_level*>::const_iterator it = m_children.constBegin();
         it != m_children.constEnd();
         ++it) {
        if (auto client = (*it)->clientForId(child)) {
            return client;
        }
    }
    // not found
    return nullptr;
}

client_model::client_model(QObject* parent)
    : QAbstractItemModel(parent)
    , m_root(nullptr)
    , m_exclusions(NoExclusion)
{
}

client_model::~client_model()
{
}

void client_model::setLevels(QList<client_model::LevelRestriction> restrictions)
{
    beginResetModel();
    if (m_root) {
        delete m_root;
    }
    m_root = abstract_level::create(restrictions, NoRestriction, this);
    connect(m_root, &abstract_level::beginInsert, this, &client_model::levelBeginInsert);
    connect(m_root, &abstract_level::beginRemove, this, &client_model::levelBeginRemove);
    connect(m_root, &abstract_level::endInsert, this, &client_model::levelEndInsert);
    connect(m_root, &abstract_level::endRemove, this, &client_model::levelEndRemove);
    m_root->init();
    endResetModel();
}

void client_model::setExclusions(client_model::Exclusions exclusions)
{
    if (exclusions == m_exclusions) {
        return;
    }
    m_exclusions = exclusions;
    Q_EMIT exclusionsChanged();
}

QVariant client_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0) {
        return QVariant();
    }
    if (auto const level = getLevel(index)) {
        LevelRestriction restriction = level->restriction();
        if (restriction == ActivityRestriction
            && (role == Qt::DisplayRole || role == ActivityRole)) {
            return level->activity();
        } else if (restriction == VirtualDesktopRestriction
                   && (role == Qt::DisplayRole || role == DesktopRole)) {
            return level->virtualDesktop();
        } else if (restriction == ScreenRestriction
                   && (role == Qt::DisplayRole || role == ScreenRole)) {
            return level->screen();
        } else {
            return QVariant();
        }
    }
    if (role == Qt::DisplayRole || role == ClientRole) {
        if (auto client = m_root->clientForId(index.internalId())) {
            return QVariant::fromValue(client);
        }
    }
    return QVariant();
}

int client_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int client_model::rowCount(const QModelIndex& parent) const
{
    if (!m_root) {
        return 0;
    }
    if (!parent.isValid()) {
        return m_root->count();
    }
    if (auto const level = getLevel(parent)) {
        if (level->id() != parent.internalId()) {
            // not a real level - no children
            return 0;
        }
        return level->count();
    }
    return 0;
}

QHash<int, QByteArray> client_model::roleNames() const
{
    return {
        {Qt::DisplayRole, QByteArrayLiteral("display")},
        {ClientRole, QByteArrayLiteral("client")},
        {ScreenRole, QByteArrayLiteral("screen")},
        {DesktopRole, QByteArrayLiteral("desktop")},
        {ActivityRole, QByteArrayLiteral("activity")},
    };
}

QModelIndex client_model::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.column() != 0) {
        return QModelIndex();
    }
    return parentForId(child.internalId());
}

QModelIndex client_model::parentForId(quint32 childId) const
{
    if (childId == m_root->id()) {
        // asking for parent of our toplevel
        return QModelIndex();
    }
    if (auto parentLevel = m_root->parentForId(childId)) {
        if (parentLevel == m_root) {
            return QModelIndex();
        }
        const int row = m_root->rowForId(parentLevel->id());
        if (row == -1) {
            // error
            return QModelIndex();
        }
        return createIndex(row, 0, parentLevel->id());
    }
    return QModelIndex();
}

QModelIndex client_model::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0 || !m_root) {
        return QModelIndex();
    }
    if (!parent.isValid()) {
        if (row >= rowCount()) {
            return QModelIndex();
        }
        return createIndex(row, 0, m_root->idForRow(row));
    }
    auto const parentLevel = getLevel(parent);
    if (!parentLevel) {
        return QModelIndex();
    }
    if (row >= parentLevel->count()) {
        return QModelIndex();
    }
    const quint32 id = parentLevel->idForRow(row);
    if (id == 0) {
        return QModelIndex();
    }
    return createIndex(row, column, id);
}

abstract_level const* client_model::getLevel(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return m_root;
    }
    return m_root->levelForId(index.internalId());
}

void client_model::levelBeginInsert(int rowStart, int rowEnd, quint32 id)
{
    const int row = m_root->rowForId(id);
    QModelIndex parent;
    if (row != -1) {
        parent = createIndex(row, 0, id);
    }
    beginInsertRows(parent, rowStart, rowEnd);
}

void client_model::levelBeginRemove(int rowStart, int rowEnd, quint32 id)
{
    const int row = m_root->rowForId(id);
    QModelIndex parent;
    if (row != -1) {
        parent = createIndex(row, 0, id);
    }
    beginRemoveRows(parent, rowStart, rowEnd);
}

void client_model::levelEndInsert()
{
    endInsertRows();
}

void client_model::levelEndRemove()
{
    endRemoveRows();
}

#define CLIENT_MODEL_WRAPPER(name, levels)                                                         \
    name::name(QObject* parent)                                                                    \
        : client_model(parent)                                                                     \
    {                                                                                              \
        setLevels(levels);                                                                         \
    }                                                                                              \
    name::~name()                                                                                  \
    {                                                                                              \
    }

CLIENT_MODEL_WRAPPER(simple_client_model, QList<LevelRestriction>())
CLIENT_MODEL_WRAPPER(client_model_by_screen, QList<LevelRestriction>() << ScreenRestriction)
CLIENT_MODEL_WRAPPER(client_model_by_screen_and_desktop,
                     QList<LevelRestriction>() << ScreenRestriction << VirtualDesktopRestriction)
CLIENT_MODEL_WRAPPER(client_model_by_screen_and_activity,
                     QList<LevelRestriction>() << ScreenRestriction << ActivityRestriction)
#undef CLIENT_MODEL_WRAPPER

client_filter_model::client_filter_model(QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_client_model(nullptr)
{
}

client_filter_model::~client_filter_model()
{
}

void client_filter_model::setClientModel(client_model* model)
{
    if (model == m_client_model) {
        return;
    }
    m_client_model = model;
    setSourceModel(m_client_model);
    Q_EMIT clientModelChanged();
}

void client_filter_model::setFilter(const QString& filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
    invalidateFilter();
}

bool client_filter_model::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!m_client_model) {
        return false;
    }
    if (m_filter.isEmpty()) {
        return true;
    }
    QModelIndex index = m_client_model->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }
    QVariant data = index.data();
    if (!data.isValid()) {
        // an invalid QVariant is valid data
        return true;
    }
    // TODO: introduce a type as a data role and properly check, this seems dangerous
    if (data.type() == QVariant::Int || data.type() == QVariant::UInt
        || data.type() == QVariant::String) {
        // we do not filter out screen, desktop and activity
        return true;
    }
    auto client = qvariant_cast<Toplevel*>(data);
    if (!client) {
        return false;
    }
    if (win::caption(client).contains(m_filter, Qt::CaseInsensitive)) {
        return true;
    }
    const QString windowRole(QString::fromUtf8(client->windowRole()));
    if (windowRole.contains(m_filter, Qt::CaseInsensitive)) {
        return true;
    }
    const QString resourceName(QString::fromUtf8(client->resourceName()));
    if (resourceName.contains(m_filter, Qt::CaseInsensitive)) {
        return true;
    }
    const QString resourceClass(QString::fromUtf8(client->resourceClass()));
    if (resourceClass.contains(m_filter, Qt::CaseInsensitive)) {
        return true;
    }
    return false;
}

}
