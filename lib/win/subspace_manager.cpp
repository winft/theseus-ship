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
#include <KLocalizedString>
#include <QAction>
#include <algorithm>

namespace KWin::win
{

static const double GESTURE_SWITCH_THRESHOLD = .25;

subspace_manager_qobject::subspace_manager_qobject() = default;

subspace_manager::subspace_manager()
    : qobject{std::make_unique<subspace_manager_qobject>()}
    , singleton{qobject.get(),
                [this] { return subspaces; },
                [this](auto pos, auto const& name) { return create_subspace(pos, name); },
                [this](auto id) { return remove_subspace(id); },
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
    m_rootInfo = info;

    // Nothing will be connected to rootInfo
    if (m_rootInfo) {
        int columns = subspaces.size() / m_rows;
        if (subspaces.size() % m_rows > 0) {
            columns++;
        }

        m_rootInfo->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);
        updateRootInfo();
        updateLayout();
        m_rootInfo->setCurrentDesktop(current_x11id());

        for (auto vd : qAsConst(subspaces)) {
            m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
        }
    }
}

QString subspace_manager::name(uint sub) const
{
    if (subspaces.size() > sub - 1) {
        return subspaces[sub - 1]->name();
    }

    if (!m_rootInfo) {
        return defaultName(sub);
    }
    return QString::fromUtf8(m_rootInfo->desktopName(sub));
}

uint subspace_manager::above(uint id, bool wrap) const
{
    auto vd = above(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::above(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);

    if (!desktop) {
        desktop = current;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);

    while (true) {
        coords.ry()--;
        if (coords.y() < 0) {
            if (wrap) {
                coords.setY(m_grid.height() - 1);
            } else {
                // Already at the top-most desktop
                return desktop;
            }
        }

        if (auto vd = m_grid.at(coords)) {
            return vd;
        }
    }

    return nullptr;
}

uint subspace_manager::toRight(uint id, bool wrap) const
{
    auto vd = toRight(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::toRight(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);

    if (!desktop) {
        desktop = current;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);

    while (true) {
        coords.rx()++;
        if (coords.x() >= m_grid.width()) {
            if (wrap) {
                coords.setX(0);
            } else {
                // Already at the right-most desktop
                return desktop;
            }
        }

        if (auto vd = m_grid.at(coords)) {
            return vd;
        }
    }

    return nullptr;
}

uint subspace_manager::below(uint id, bool wrap) const
{
    auto vd = below(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::below(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);

    if (!desktop) {
        desktop = current;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);

    while (true) {
        coords.ry()++;
        if (coords.y() >= m_grid.height()) {
            if (wrap) {
                coords.setY(0);
            } else {
                // Already at the bottom-most desktop
                return desktop;
            }
        }

        if (auto vd = m_grid.at(coords)) {
            return vd;
        }
    }

    return nullptr;
}

uint subspace_manager::toLeft(uint id, bool wrap) const
{
    auto vd = toLeft(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::toLeft(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);

    if (!desktop) {
        desktop = current;
    }

    QPoint coords = m_grid.gridCoords(desktop);
    Q_ASSERT(coords.x() >= 0);

    while (true) {
        coords.rx()--;
        if (coords.x() < 0) {
            if (wrap) {
                coords.setX(m_grid.width() - 1);
            } else {
                return desktop; // Already at the left-most desktop
            }
        }

        if (auto vd = m_grid.at(coords)) {
            return vd;
        }
    }

    return nullptr;
}

subspace* subspace_manager::next(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);
    if (!desktop) {
        desktop = current;
    }

    auto it = std::find(subspaces.begin(), subspaces.end(), desktop);
    Q_ASSERT(it != subspaces.end());
    it++;

    if (it == subspaces.end()) {
        if (wrap) {
            return subspaces.front();
        } else {
            return desktop;
        }
    }

    return *it;
}

uint subspace_manager::next(uint id, bool wrap) const
{
    auto vd = next(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::previous(subspace* desktop, bool wrap) const
{
    Q_ASSERT(current);
    if (!desktop) {
        desktop = current;
    }

    auto it = std::find(subspaces.begin(), subspaces.end(), desktop);
    Q_ASSERT(it != subspaces.end());

    if (it == subspaces.begin()) {
        if (wrap) {
            return subspaces.back();
        } else {
            return desktop;
        }
    }

    it--;
    return *it;
}

uint subspace_manager::previous(uint id, bool wrap) const
{
    auto vd = previous(subspace_for_x11id(id), wrap);
    return vd ? vd->x11DesktopNumber() : 0;
}

subspace* subspace_manager::subspace_for_x11id(uint id) const
{
    if (id == 0 || id > subspaces.size()) {
        return nullptr;
    }
    return subspaces.at(id - 1);
}

subspace* subspace_manager::subspace_for_id(QString const& id) const
{
    auto desk = std::find_if(
        subspaces.begin(), subspaces.end(), [id](auto desk) { return desk->id() == id; });

    if (desk != subspaces.end()) {
        return *desk;
    }

    return nullptr;
}

subspace* subspace_manager::create_subspace(uint position, QString const& name)
{
    // too many, can't insert new ones
    if (subspaces.size() == subspace_manager::maximum()) {
        return nullptr;
    }

    position = std::clamp<uint>(0u, position, subspaces.size());

    QString desktopName = name;
    if (desktopName.isEmpty()) {
        desktopName = defaultName(position + 1);
    }

    auto vd = add_subspace(position, "", desktopName);

    save();
    updateRootInfo();
    updateLayout();

    Q_EMIT qobject->subspace_created(vd);
    Q_EMIT qobject->countChanged(subspaces.size() - 1, subspaces.size());

    return vd;
}

void subspace_manager::remove_subspace(QString const& id)
{
    auto sub = subspace_for_id(id);
    if (sub) {
        remove_subspace(sub);
    }
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
        subspaces[j]->setX11DesktopNumber(j + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(j + 1, subspaces[j]->name().toUtf8().data());
        }
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

uint subspace_manager::current_x11id() const
{
    return current ? current->x11DesktopNumber() : 0;
}

bool subspace_manager::setCurrent(uint newDesktop)
{
    if (newDesktop < 1 || newDesktop > subspaces.size()) {
        return false;
    }

    auto d = subspace_for_x11id(newDesktop);
    Q_ASSERT(d);
    return setCurrent(d);
}

bool subspace_manager::setCurrent(subspace* newDesktop)
{
    Q_ASSERT(newDesktop);
    if (current == newDesktop) {
        return false;
    }

    auto old_subsp = current;
    current = newDesktop;

    Q_EMIT qobject->current_changed(old_subsp, current);
    return true;
}

subspace* subspace_manager::add_subspace(size_t position, QString const& id, QString const& name)
{
    auto subsp = new subspace(id, qobject.get());
    subsp->setX11DesktopNumber(position + 1);
    subsp->setName(name);

    subspaces.insert(subspaces.begin() + position, subsp);

    QObject::connect(subsp, &subspace::nameChanged, qobject.get(), [this, subsp]() {
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(subsp->x11DesktopNumber(), subsp->name().toUtf8().data());
        }
    });

    if (m_rootInfo) {
        m_rootInfo->setDesktopName(subsp->x11DesktopNumber(), subsp->name().toUtf8().data());
    }

    // update the id of displaced subspaces
    for (size_t i = position + 1; i < subspaces.size(); ++i) {
        auto& other = subspaces.at(i);
        other->setX11DesktopNumber(i + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(i + 1, other->name().toUtf8().data());
        }
    }

    return subsp;
}

void subspace_manager::shrink_subspaces(uint count)
{
    if (count >= subspaces.size()) {
        return;
    }

    auto const subspacesToRemove
        = std::vector<subspace*>{subspaces.begin() + count, subspaces.end()};
    subspaces.resize(count);

    assert(current);
    auto old_subsp = current;
    auto oldCurrent = current_x11id();
    auto newCurrent = std::min<uint>(oldCurrent, count);

    current = subspaces.at(newCurrent - 1);

    if (oldCurrent != newCurrent) {
        Q_EMIT qobject->current_changed(old_subsp, current);
    }

    for (auto desktop : subspacesToRemove) {
        Q_EMIT qobject->subspace_removed(desktop);
        desktop->deleteLater();
    }
}

void subspace_manager::setCount(uint count)
{
    count = std::clamp<uint>(1, count, subspace_manager::maximum());
    if (count == subspaces.size()) {
        // nothing to change
        return;
    }

    auto const oldCount = subspaces.size();

    shrink_subspaces(count);

    while (subspaces.size() < count) {
        auto const position = subspaces.size();
        add_subspace(position, "", defaultName(position + 1));
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

    if (m_rootInfo) {
        m_rootInfo->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);
        m_rootInfo->activate();
    }

    updateLayout();
}

void subspace_manager::updateRootInfo()
{
    if (!m_rootInfo) {
        return;
    }

    const int n = subspaces.size();
    m_rootInfo->setNumberOfDesktops(n);

    auto viewports = new x11::net::point[n];
    m_rootInfo->setDesktopViewport(n, *viewports);
    delete[] viewports;
}

void subspace_manager::updateLayout()
{
    m_rows = std::min<uint>(m_rows, subspaces.size());

    int columns = subspaces.size() / m_rows;
    Qt::Orientation orientation = Qt::Horizontal;

    if (m_rootInfo) {
        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = m_rootInfo->desktopLayoutColumnsRows().width();
        m_rows = std::max<int>(1, m_rootInfo->desktopLayoutColumnsRows().height());
        orientation = m_rootInfo->desktopLayoutOrientation() == x11::net::OrientationHorizontal
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
    m_grid.update(QSize(columns, m_rows), orientation, subspaces);

    // TODO: why is there no call to m_rootInfo->setDesktopLayout?
    Q_EMIT qobject->layoutChanged(columns, m_rows);
    Q_EMIT qobject->rowsChanged(m_rows);
}

void subspace_manager::load()
{
    if (!m_config) {
        return;
    }

    KConfigGroup group(m_config, QStringLiteral("Desktops"));

    size_t const oldCount = subspaces.size();
    size_t const count = group.readEntry("Number", 1);

    shrink_subspaces(count);

    auto set_name = [&, this](auto index) {
        auto const x11id = index + 1;
        QString name
            = group.readEntry(QStringLiteral("Name_%1").arg(x11id), i18n("Desktop %1", x11id));
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(x11id, name.toUtf8().data());
        }
        subspaces[index]->setName(name);
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
    if (!m_config) {
        return;
    }

    KConfigGroup group(m_config, QStringLiteral("Desktops"));

    for (int i = subspaces.size() + 1; group.hasKey(QStringLiteral("Id_%1").arg(i)); i++) {
        group.deleteEntry(QStringLiteral("Id_%1").arg(i));
        group.deleteEntry(QStringLiteral("Name_%1").arg(i));
    }

    group.writeEntry("Number", static_cast<int>(subspaces.size()));

    for (auto i = 1u; i <= subspaces.size(); ++i) {
        QString s = name(i);
        auto const defaultvalue = defaultName(i);

        if (s.isEmpty()) {
            s = defaultvalue;
            if (m_rootInfo) {
                m_rootInfo->setDesktopName(i, s.toUtf8().data());
            }
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

QString subspace_manager::defaultName(int desktop) const
{
    return i18n("Desktop %1", desktop);
}

void subspace_manager::connect_gestures()
{
    QObject::connect(swipe_gesture.released_x.get(), &QAction::triggered, qobject.get(), [this]() {
        // Note that if desktop wrapping is disabled and there's no desktop to left or right,
        // toLeft() and toRight() will return the current desktop.
        auto target = current;

        if (current_desktop_offset.x() <= -GESTURE_SWITCH_THRESHOLD) {
            target = toLeft(current, isNavigationWrappingAround());
        } else if (current_desktop_offset.x() >= GESTURE_SWITCH_THRESHOLD) {
            target = toRight(current, isNavigationWrappingAround());
        }

        // If the current desktop has not changed, consider that the gesture has been canceled.
        if (current != target) {
            setCurrent(target);
        } else {
            Q_EMIT qobject->current_changing_cancelled();
        }
        current_desktop_offset = QPointF(0, 0);
    });

    QObject::connect(swipe_gesture.released_y.get(), &QAction::triggered, qobject.get(), [this]() {
        // Note that if desktop wrapping is disabled and there's no desktop above or below,
        // above() and below() will return the current desktop.
        subspace* target = current;
        if (current_desktop_offset.y() <= -GESTURE_SWITCH_THRESHOLD) {
            target = above(current, isNavigationWrappingAround());
        } else if (current_desktop_offset.y() >= GESTURE_SWITCH_THRESHOLD) {
            target = below(current, isNavigationWrappingAround());
        }

        // If the current desktop has not changed, consider that the gesture has been canceled.
        if (current != target) {
            setCurrent(target);
        } else {
            Q_EMIT qobject->current_changing_cancelled();
        }
        current_desktop_offset = QPointF(0, 0);
    });
}

void subspace_manager::slotSwitchTo(QAction& action)
{
    bool ok = false;
    auto const i = action.data().toUInt(&ok);
    if (ok) {
        setCurrent(i);
    }
}

void subspace_manager::setNavigationWrappingAround(bool enabled)
{
    if (enabled == m_navigationWrapsAround) {
        return;
    }

    m_navigationWrapsAround = enabled;
    Q_EMIT qobject->navigationWrappingAroundChanged();
}

void subspace_manager::slotDown()
{
    setCurrent(below(nullptr, isNavigationWrappingAround()));
}

void subspace_manager::slotLeft()
{
    setCurrent(toLeft(nullptr, isNavigationWrappingAround()));
}

void subspace_manager::slotPrevious()
{
    setCurrent(previous(nullptr, isNavigationWrappingAround()));
}

void subspace_manager::slotNext()
{
    setCurrent(next(nullptr, isNavigationWrappingAround()));
}

void subspace_manager::slotRight()
{
    setCurrent(toRight(nullptr, isNavigationWrappingAround()));
}

void subspace_manager::slotUp()
{
    setCurrent(above(nullptr, isNavigationWrappingAround()));
}

}
