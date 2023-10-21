/*
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace_manager.h"

#include "singleton_interface.h"
#include "x11/net/root_info.h"

#include <KConfigGroup>
#include <algorithm>

namespace KWin::win
{

subspace_manager_qobject::subspace_manager_qobject() = default;

subspace_manager::subspace_manager()
    : qobject{std::make_unique<subspace_manager_qobject>()}
    , singleton{qobject.get(),
                [this] { return subspaces; },
                [this](auto pos, auto const& name) { return create_subspace(pos, name); },
                [this](auto id) {
                    if (auto sub = subspaces_get_for_id(*this, id)) {
                        remove_subspace(sub);
                    }
                },
                [this] { return current; }}

{
    singleton_interface::subspaces = &singleton;

    swipe_gesture.released_x = std::make_unique<QAction>();
    swipe_gesture.released_y = std::make_unique<QAction>();
}

subspace_manager::~subspace_manager()
{
    singleton_interface::subspaces = {};
}

void subspace_manager::setRootInfo(x11::net::root_info* info)
{
    root_info = info;

    // Nothing will be connected to rootInfo
    if (!root_info) {
        return;
    }

    int columns = subspaces.size() / m_rows;
    if (subspaces.size() % m_rows > 0) {
        columns++;
    }

    root_info->setDesktopLayout(
        x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);

    updateRootInfo();
    updateLayout();
    root_info->setCurrentDesktop(subspaces_get_current_x11id(*this));

    for (auto vd : qAsConst(subspaces)) {
        root_info->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
    }
}

QString subspace_manager::name(uint sub) const
{
    if (subspaces.size() > sub - 1) {
        return subspaces[sub - 1]->name();
    }

    if (!root_info) {
        return subspace_manager_get_default_subspace_name(sub);
    }
    return QString::fromUtf8(root_info->desktopName(sub));
}

subspace* subspace_manager::create_subspace(uint position, QString const& name)
{
    // too many, can't insert new ones
    if (subspaces.size() == subspace_manager::max_count) {
        return nullptr;
    }

    position = std::clamp<uint>(0u, position, subspaces.size());

    QString desktopName = name;
    if (desktopName.isEmpty()) {
        desktopName = subspace_manager_get_default_subspace_name(position + 1);
    }

    auto vd = add_subspace(position, "", desktopName);

    save();
    updateRootInfo();
    updateLayout();

    Q_EMIT qobject->subspace_created(vd);
    Q_EMIT qobject->countChanged(subspaces.size() - 1, subspaces.size());

    return vd;
}

void subspace_manager::remove_subspace(subspace* sub)
{
    assert(sub);

    // don't end up without any subspace
    if (subspaces.size() == 1) {
        return;
    }

    assert(current);
    auto old_subsp = current;
    auto const oldCurrent = old_subsp->x11DesktopNumber();

    auto const i = sub->x11DesktopNumber() - 1;
    subspaces.erase(subspaces.begin() + i);

    for (auto j = i; j < subspaces.size(); ++j) {
        auto subsp = subspaces.at(j);
        subspace_manager_update_subspace_meta(*this, subsp, subsp->name(), j + 1);
    }

    auto const newCurrent = std::min<uint>(oldCurrent, subspaces.size());
    current = subspaces.at(newCurrent - 1);

    if (oldCurrent != newCurrent) {
        Q_EMIT qobject->current_changed(old_subsp, current);
    }

    updateRootInfo();
    updateLayout();
    save();

    Q_EMIT qobject->subspace_removed(sub);
    Q_EMIT qobject->countChanged(subspaces.size() + 1, subspaces.size());

    sub->deleteLater();
}

subspace* subspace_manager::add_subspace(size_t position, QString const& id, QString const& name)
{
    auto subsp = new subspace(id, qobject.get());
    subspaces.insert(subspaces.begin() + position, subsp);
    subspace_manager_update_subspace_meta(*this, subsp, name, position + 1);

    QObject::connect(subsp, &subspace::nameChanged, qobject.get(), [this, subsp]() {
        subspace_manager_update_subspace_meta(
            *this, subsp, subsp->name(), subsp->x11DesktopNumber());
    });

    // update the id of displaced subspaces
    for (auto i = position + 1; i < subspaces.size(); ++i) {
        subspace_manager_update_subspace_meta(*this, subsp, subsp->name(), i + 1);
    }

    return subsp;
}

void subspace_manager::setCount(uint count)
{
    count = std::clamp<uint>(1, count, subspace_manager::max_count);
    if (count == subspaces.size()) {
        // nothing to change
        return;
    }

    auto const oldCount = subspaces.size();

    subspace_manager_shrink_subspaces(*this, count);

    while (subspaces.size() < count) {
        auto const position = subspaces.size();
        add_subspace(position, "", subspace_manager_get_default_subspace_name(position + 1));
    }

    updateRootInfo();
    updateLayout();
    save();

    for (auto index = oldCount; index < subspaces.size(); index++) {
        Q_EMIT qobject->subspace_created(subspaces.at(index));
    }

    Q_EMIT qobject->countChanged(oldCount, subspaces.size());
}

uint subspace_manager::rows() const
{
    return m_rows;
}

void subspace_manager::setRows(uint rows)
{
    if (rows == 0 || rows > subspaces.size() || rows == m_rows) {
        return;
    }

    m_rows = rows;
    auto columns = subspaces.size() / m_rows;

    if (subspaces.size() % m_rows > 0) {
        columns++;
    }

    if (root_info) {
        root_info->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);
        root_info->activate();
    }

    updateLayout();
}

void subspace_manager::updateRootInfo()
{
    if (!root_info) {
        return;
    }

    const int n = subspaces.size();
    root_info->setNumberOfDesktops(n);

    auto viewports = new x11::net::point[n];
    root_info->setDesktopViewport(n, *viewports);
    delete[] viewports;
}

void subspace_manager::updateLayout()
{
    m_rows = std::min<uint>(m_rows, subspaces.size());

    int columns = subspaces.size() / m_rows;
    Qt::Orientation orientation = Qt::Horizontal;

    if (root_info) {
        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = root_info->desktopLayoutColumnsRows().width();
        m_rows = std::max<int>(1, root_info->desktopLayoutColumnsRows().height());
        orientation = root_info->desktopLayoutOrientation() == x11::net::OrientationHorizontal
            ? Qt::Horizontal
            : Qt::Vertical;
    }

    if (columns == 0) {
        // Not given, set default layout
        m_rows = subspaces.size() == 1u ? 1 : 2;
        columns = subspaces.size() / m_rows;
    }

    // Patch to make desktop grid size equal 1 when 1 desktop for desktop switching animations
    if (subspaces.size() == 1) {
        m_rows = 1;
        columns = 1;
    }

    // Calculate valid grid size
    Q_ASSERT(columns > 0 || m_rows > 0);

    if ((columns <= 0) && (m_rows > 0)) {
        columns = (subspaces.size() + m_rows - 1) / m_rows;
    } else if ((m_rows <= 0) && (columns > 0)) {
        m_rows = (subspaces.size() + columns - 1) / columns;
    }

    while (columns * m_rows < subspaces.size()) {
        if (orientation == Qt::Horizontal) {
            ++columns;
        } else {
            ++m_rows;
        }
    }

    m_rows = std::max<uint>(1u, m_rows);
    grid.update(QSize(columns, m_rows), orientation, subspaces);

    // TODO: why is there no call to root_info->setDesktopLayout?
    Q_EMIT qobject->layoutChanged(columns, m_rows);
    Q_EMIT qobject->rowsChanged(m_rows);
}

void subspace_manager::load()
{
    if (!config) {
        return;
    }

    KConfigGroup group(config, QStringLiteral("Desktops"));

    size_t const oldCount = subspaces.size();
    size_t const count = group.readEntry("Number", 1);

    subspace_manager_shrink_subspaces(*this, count);

    auto set_name = [&, this](auto index) {
        auto const x11id = index + 1;
        QString name
            = group.readEntry(QStringLiteral("Name_%1").arg(x11id), i18n("Desktop %1", x11id));
        subspace_manager_update_subspace_meta(*this, subspaces.at(index), name, x11id);
    };

    auto get_id = [&](auto index) {
        return group.readEntry(QStringLiteral("Id_%1").arg(index + 1), QString());
    };

    for (size_t index = 1; index < std::min(oldCount, count); index++) {
        assert(subspaces[index]->id() == get_id(index) || get_id(index).isEmpty());
        set_name(index);
    }

    for (size_t index = oldCount; index < count; index++) {
        add_subspace(index, get_id(index), "");
        set_name(index);
    }

    assert(count == subspaces.size());

    updateRootInfo();
    updateLayout();

    for (size_t index = oldCount; index < subspaces.size(); index++) {
        Q_EMIT qobject->subspace_created(subspaces.at(index));
    }

    Q_EMIT qobject->countChanged(oldCount, subspaces.size());

    m_rows = std::clamp<int>(1, group.readEntry<int>("Rows", 2), subspaces.size());
}

void subspace_manager::save()
{
    if (!config) {
        return;
    }

    KConfigGroup group(config, QStringLiteral("Desktops"));

    for (int i = subspaces.size() + 1; group.hasKey(QStringLiteral("Id_%1").arg(i)); i++) {
        group.deleteEntry(QStringLiteral("Id_%1").arg(i));
        group.deleteEntry(QStringLiteral("Name_%1").arg(i));
    }

    group.writeEntry("Number", static_cast<int>(subspaces.size()));

    for (auto i = 1u; i <= subspaces.size(); ++i) {
        QString s = name(i);
        auto const defaultvalue = subspace_manager_get_default_subspace_name(i);

        if (s.isEmpty()) {
            s = defaultvalue;
            subspace_manager_update_subspace_meta(*this, subspaces.at(i - 1), s, i);
        }

        if (s != defaultvalue) {
            group.writeEntry(QStringLiteral("Name_%1").arg(i), s);
        } else {
            QString currentvalue = group.readEntry(QStringLiteral("Name_%1").arg(i), QString());
            if (currentvalue != defaultvalue) {
                group.deleteEntry(QStringLiteral("Name_%1").arg(i));
            }
        }
        group.writeEntry(QStringLiteral("Id_%1").arg(i), subspaces[i - 1]->id());
    }

    group.writeEntry("Rows", m_rows);

    // Save to disk
    group.sync();
}

}
