/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_model.h"

#include "base/singleton_interface.h"
#include "script/platform.h"
#include "script/singleton_interface.h"
#include "script/space.h"

namespace KWin::scripting
{

window_model::window_model(QObject* parent)
    : QAbstractListModel(parent)
{
    auto ws_wrap = singleton_interface::qt_script_space;

    connect(ws_wrap, &space::windowAdded, this, &window_model::handleWindowAdded);
    connect(ws_wrap, &space::windowRemoved, this, &window_model::handleWindowRemoved);

    for (auto window : ws_wrap->get_windows()) {
        m_windows << window->internalId();
        setupWindowConnections(window);
    }
}

void window_model::markRoleChanged(scripting::window* window, int role)
{
    const QModelIndex row = index(m_windows.indexOf(window->internalId()), 0);
    Q_EMIT dataChanged(row, row, {role});
}

void window_model::setupWindowConnections(scripting::window* window)
{
    connect(window, &window::desktopsChanged, this, [this, window]() {
        markRoleChanged(window, DesktopRole);
    });
    connect(window, &window::outputChanged, this, [this, window]() {
        markRoleChanged(window, OutputRole);
    });
}

void window_model::handleWindowAdded(scripting::window* window)
{
    beginInsertRows(QModelIndex(), m_windows.count(), m_windows.count());
    m_windows.append(window->internalId());
    endInsertRows();

    setupWindowConnections(window);
}

void window_model::handleWindowRemoved(scripting::window* window)
{
    const int index = m_windows.indexOf(window->internalId());
    Q_ASSERT(index != -1);

    beginRemoveRows(QModelIndex(), index, index);
    m_windows.removeAt(index);
    endRemoveRows();
}

QHash<int, QByteArray> window_model::roleNames() const
{
    return {
        {Qt::DisplayRole, QByteArrayLiteral("display")},
        {WindowRole, QByteArrayLiteral("window")},
        {OutputRole, QByteArrayLiteral("output")},
        {DesktopRole, QByteArrayLiteral("desktop")},
        {ActivityRole, QByteArrayLiteral("activity")},
    };
}

scripting::window* find_window(QUuid const& wId)
{
    auto const windows = scripting::singleton_interface::qt_script_space->windowList();
    for (auto win : windows) {
        if (win->internalId() == wId) {
            return win;
        }
    }
    return nullptr;
}

QVariant window_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.count()) {
        return QVariant();
    }

    auto window = find_window(m_windows[index.row()]);
    if (!window) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case WindowRole:
        return QVariant::fromValue(window);
    case OutputRole:
        return QVariant::fromValue(window->output());
    case DesktopRole:
        return QVariant::fromValue(window->desktops());
    case ActivityRole:
        return window->activities();
    default:
        return {};
    }
}

int window_model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_windows.count();
}

window_filter_model::window_filter_model(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

window_model* window_filter_model::windowModel() const
{
    return m_windowModel;
}

void window_filter_model::setWindowModel(window_model* model)
{
    if (model == m_windowModel) {
        return;
    }
    m_windowModel = model;
    setSourceModel(m_windowModel);
    Q_EMIT windowModelChanged();
}

QString window_filter_model::activity() const
{
    return {};
}

void window_filter_model::setActivity(const QString& /*activity*/)
{
}

void window_filter_model::resetActivity()
{
}

win::subspace* window_filter_model::desktop() const
{
    return m_desktop;
}

void window_filter_model::setDesktop(win::subspace* sub)
{
    if (m_desktop != sub) {
        m_desktop = sub;
        Q_EMIT desktopChanged();
        invalidateFilter();
    }
}

void window_filter_model::resetDesktop()
{
    setDesktop(nullptr);
}

QString window_filter_model::filter() const
{
    return m_filter;
}

void window_filter_model::setFilter(const QString& filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
    invalidateFilter();
}

QString window_filter_model::screenName() const
{
    return m_output ? m_output->name() : QString();
}

void window_filter_model::setScreenName(const QString& screen)
{
    auto const& outputs = base::singleton_interface::get_outputs();
    auto output = base::find_output(outputs, screen);
    if (m_output != output) {
        m_output = output;
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

void window_filter_model::resetScreenName()
{
    if (m_output) {
        m_output = nullptr;
        Q_EMIT screenNameChanged();
        invalidateFilter();
    }
}

window_filter_model::WindowTypes window_filter_model::windowType() const
{
    return m_windowType.value_or(WindowTypes());
}

void window_filter_model::setWindowType(WindowTypes windowType)
{
    if (m_windowType != windowType) {
        m_windowType = windowType;
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

void window_filter_model::resetWindowType()
{
    if (m_windowType.has_value()) {
        m_windowType.reset();
        Q_EMIT windowTypeChanged();
        invalidateFilter();
    }
}

void window_filter_model::setMinimizedWindows(bool show)
{
    if (m_showMinimizedWindows == show) {
        return;
    }

    m_showMinimizedWindows = show;
    invalidateFilter();
    Q_EMIT minimizedWindowsChanged();
}

bool window_filter_model::minimizedWindows() const
{
    return m_showMinimizedWindows;
}

bool window_filter_model::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (!m_windowModel) {
        return false;
    }
    const QModelIndex index = m_windowModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }
    const QVariant data = index.data();
    if (!data.isValid()) {
        // an invalid QVariant is valid data
        return true;
    }

    auto window = qvariant_cast<scripting::window*>(data);
    if (!window) {
        return false;
    }

    if (m_desktop) {
        if (!window->isOnDesktop(m_desktop)) {
            return false;
        }
    }

    if (m_output) {
        if (!window->isOnOutput(m_output)) {
            return false;
        }
    }

    if (m_windowType.has_value()) {
        if (!(windowTypeMask(window) & *m_windowType)) {
            return false;
        }
    }

    if (!m_filter.isEmpty()) {
        if (window->caption().contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        if (window->windowRole().contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        if (window->resourceName().contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        if (window->resourceClass().contains(m_filter, Qt::CaseInsensitive)) {
            return true;
        }
        return false;
    }

    if (!m_showMinimizedWindows) {
        return !window->isMinimized();
    }
    return true;
}

window_filter_model::WindowTypes
window_filter_model::windowTypeMask(scripting::window* window) const
{
    WindowTypes mask;
    if (window->isNormalWindow()) {
        mask |= WindowType::Normal;
    } else if (window->isDialog()) {
        mask |= WindowType::Dialog;
    } else if (window->isDock()) {
        mask |= WindowType::Dock;
    } else if (window->isDesktop()) {
        mask |= WindowType::Desktop;
    } else if (window->isNotification()) {
        mask |= WindowType::Notification;
    } else if (window->isCriticalNotification()) {
        mask |= WindowType::CriticalNotification;
    }
    return mask;
}

}
