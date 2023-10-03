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
#include <QUuid>
#include <algorithm>

namespace KWin::win
{

static bool s_loadingDesktopSettings = false;
static const double GESTURE_SWITCH_THRESHOLD = .25;

static QString generateDesktopId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

subspace_manager_qobject::subspace_manager_qobject() = default;

subspace_manager::subspace_manager()
    : qobject{std::make_unique<subspace_manager_qobject>()}
    , m_swipeGestureReleasedY(new QAction(qobject.get()))
    , m_swipeGestureReleasedX(new QAction(qobject.get()))
    , singleton{qobject.get(),
                [this] { return subspaces(); },
                [this](auto pos, auto const& name) { return create_subspace(pos, name); },
                [this](auto id) { return remove_subspace(id); },
                [this] { return current_subspace(); }}

{
    singleton_interface::subspaces = &singleton;
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
        int columns = count() / m_rows;
        if (count() % m_rows > 0) {
            columns++;
        }

        m_rootInfo->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);
        updateRootInfo();
        updateLayout();
        m_rootInfo->setCurrentDesktop(current_subspace()->x11DesktopNumber());

        for (auto vd : qAsConst(m_subspaces)) {
            m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
        }
    }
}

QString subspace_manager::name(uint sub) const
{
    if (uint(m_subspaces.length()) > sub - 1) {
        return m_subspaces[sub - 1]->name();
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
    Q_ASSERT(m_current);

    if (!desktop) {
        desktop = m_current;
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
    Q_ASSERT(m_current);

    if (!desktop) {
        desktop = m_current;
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
    Q_ASSERT(m_current);

    if (!desktop) {
        desktop = m_current;
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
    Q_ASSERT(m_current);

    if (!desktop) {
        desktop = m_current;
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
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    auto it = std::find(m_subspaces.begin(), m_subspaces.end(), desktop);
    Q_ASSERT(it != m_subspaces.end());
    it++;

    if (it == m_subspaces.end()) {
        if (wrap) {
            return m_subspaces.first();
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
    Q_ASSERT(m_current);
    if (!desktop) {
        desktop = m_current;
    }

    auto it = std::find(m_subspaces.begin(), m_subspaces.end(), desktop);
    Q_ASSERT(it != m_subspaces.end());

    if (it == m_subspaces.begin()) {
        if (wrap) {
            return m_subspaces.last();
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
    if (id == 0 || id > count()) {
        return nullptr;
    }
    return m_subspaces.at(id - 1);
}

subspace* subspace_manager::subspace_for_id(QString const& id) const
{
    auto desk = std::find_if(m_subspaces.constBegin(), m_subspaces.constEnd(), [id](auto desk) {
        return desk->id() == id;
    });

    if (desk != m_subspaces.constEnd()) {
        return *desk;
    }

    return nullptr;
}

subspace* subspace_manager::create_subspace(uint position, QString const& name)
{
    // too many, can't insert new ones
    if (static_cast<uint>(m_subspaces.count()) == subspace_manager::maximum()) {
        return nullptr;
    }

    position = qBound(0u, position, static_cast<uint>(m_subspaces.count()));

    QString desktopName = name;
    if (desktopName.isEmpty()) {
        desktopName = defaultName(position + 1);
    }

    auto vd = new subspace(qobject.get());
    vd->setX11DesktopNumber(position + 1);
    vd->setId(generateDesktopId());
    vd->setName(desktopName);

    QObject::connect(vd, &subspace::nameChanged, qobject.get(), [this, vd]() {
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
        }
    });

    if (m_rootInfo) {
        m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
    }

    m_subspaces.insert(position, vd);

    // update the id of displaced subspaces
    for (uint i = position + 1; i < static_cast<uint>(m_subspaces.count()); ++i) {
        m_subspaces[i]->setX11DesktopNumber(i + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(i + 1, m_subspaces[i]->name().toUtf8().data());
        }
    }

    save();
    updateRootInfo();
    updateLayout();

    Q_EMIT qobject->subspace_created(vd);
    Q_EMIT qobject->countChanged(m_subspaces.count() - 1, m_subspaces.count());

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
    if (m_subspaces.count() == 1) {
        return;
    }

    uint const oldCurrent = m_current->x11DesktopNumber();
    uint const i = sub->x11DesktopNumber() - 1;
    m_subspaces.remove(i);

    for (uint j = i; j < static_cast<uint>(m_subspaces.count()); ++j) {
        m_subspaces[j]->setX11DesktopNumber(j + 1);
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(j + 1, m_subspaces[j]->name().toUtf8().data());
        }
    }

    uint const newCurrent = qMin(oldCurrent, static_cast<uint>(m_subspaces.count()));
    m_current = m_subspaces.at(newCurrent - 1);
    if (oldCurrent != newCurrent) {
        Q_EMIT qobject->current_changed(oldCurrent, newCurrent);
    }

    updateRootInfo();
    updateLayout();
    save();

    Q_EMIT qobject->subspace_removed(sub);
    Q_EMIT qobject->countChanged(m_subspaces.count() + 1, m_subspaces.count());

    sub->deleteLater();
}

uint subspace_manager::current() const
{
    return m_current ? m_current->x11DesktopNumber() : 0;
}

subspace* subspace_manager::current_subspace() const
{
    return m_current;
}

bool subspace_manager::setCurrent(uint newDesktop)
{
    if (newDesktop < 1 || newDesktop > count()) {
        return false;
    }

    auto d = subspace_for_x11id(newDesktop);
    Q_ASSERT(d);
    return setCurrent(d);
}

bool subspace_manager::setCurrent(subspace* newDesktop)
{
    Q_ASSERT(newDesktop);
    if (m_current == newDesktop) {
        return false;
    }

    uint const oldDesktop = current();
    m_current = newDesktop;

    Q_EMIT qobject->current_changed(oldDesktop, newDesktop->x11DesktopNumber());
    return true;
}

QList<subspace*> subspace_manager::update_count(uint count)
{
    // this explicit check makes it more readable
    if (static_cast<uint>(m_subspaces.count()) > count) {
        auto const subspacesToRemove = m_subspaces.mid(count);
        m_subspaces.resize(count);
        if (m_current) {
            uint oldCurrent = current();
            uint newCurrent = qMin(oldCurrent, count);
            m_current = m_subspaces.at(newCurrent - 1);
            if (oldCurrent != newCurrent) {
                Q_EMIT qobject->current_changed(oldCurrent, newCurrent);
            }
        }
        for (auto desktop : subspacesToRemove) {
            Q_EMIT qobject->subspace_removed(desktop);
            desktop->deleteLater();
        }

        return {};
    }

    QList<subspace*> new_subspaces;

    while (uint(m_subspaces.count()) < count) {
        auto vd = new subspace(qobject.get());
        const int x11Number = m_subspaces.count() + 1;
        vd->setX11DesktopNumber(x11Number);
        vd->setName(defaultName(x11Number));
        if (!s_loadingDesktopSettings) {
            vd->setId(generateDesktopId());
        }
        m_subspaces << vd;
        new_subspaces << vd;
        QObject::connect(vd, &subspace::nameChanged, qobject.get(), [this, vd] {
            if (m_rootInfo) {
                m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
            }
        });
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(vd->x11DesktopNumber(), vd->name().toUtf8().data());
        }
    }

    return new_subspaces;
}

void subspace_manager::setCount(uint count)
{
    count = qBound<uint>(1, count, subspace_manager::maximum());
    if (count == uint(m_subspaces.count())) {
        // nothing to change
        return;
    }

    uint const oldCount = m_subspaces.count();

    auto new_subspaces = update_count(count);

    updateRootInfo();
    updateLayout();

    if (!s_loadingDesktopSettings) {
        save();
    }
    for (auto vd : qAsConst(new_subspaces)) {
        Q_EMIT qobject->subspace_created(vd);
    }

    Q_EMIT qobject->countChanged(oldCount, m_subspaces.count());
}

uint subspace_manager::rows() const
{
    return m_rows;
}

void subspace_manager::setRows(uint rows)
{
    if (rows == 0 || rows > count() || rows == m_rows) {
        return;
    }

    m_rows = rows;
    int columns = count() / m_rows;

    if (count() % m_rows > 0) {
        columns++;
    }

    if (m_rootInfo) {
        m_rootInfo->setDesktopLayout(
            x11::net::OrientationHorizontal, columns, m_rows, x11::net::DesktopLayoutCornerTopLeft);
        m_rootInfo->activate();
    }

    updateLayout();

    // rowsChanged will be emitted by setNETDesktopLayout called by updateLayout
}

void subspace_manager::updateRootInfo()
{
    if (!m_rootInfo) {
        return;
    }

    const int n = count();
    m_rootInfo->setNumberOfDesktops(n);

    auto viewports = new x11::net::point[n];
    m_rootInfo->setDesktopViewport(n, *viewports);
    delete[] viewports;
}

void subspace_manager::updateLayout()
{
    m_rows = qMin(m_rows, count());

    int columns = count() / m_rows;
    Qt::Orientation orientation = Qt::Horizontal;

    if (m_rootInfo) {
        // TODO: Is there a sane way to avoid overriding the existing grid?
        columns = m_rootInfo->desktopLayoutColumnsRows().width();
        m_rows = qMax(1, m_rootInfo->desktopLayoutColumnsRows().height());
        orientation = m_rootInfo->desktopLayoutOrientation() == x11::net::OrientationHorizontal
            ? Qt::Horizontal
            : Qt::Vertical;
    }

    if (columns == 0) {
        // Not given, set default layout
        m_rows = count() == 1u ? 1 : 2;
        columns = count() / m_rows;
    }

    // Patch to make desktop grid size equal 1 when 1 desktop for desktop switching animations
    if (m_subspaces.size() == 1) {
        m_rows = 1;
        columns = 1;
    }

    setNETDesktopLayout(
        orientation,
        columns,
        m_rows,
        0 // rootInfo->desktopLayoutCorner() // Not really worth implementing right now.
    );
}

void subspace_manager::load()
{
    if (!m_config) {
        return;
    }

    s_loadingDesktopSettings = true;

    KConfigGroup group(m_config, QStringLiteral("Desktops"));
    const int n = group.readEntry("Number", 1);

    uint const oldCount = m_subspaces.count();
    auto new_desktops = update_count(n);

    for (int i = 1; i <= n; i++) {
        QString s = group.readEntry(QStringLiteral("Name_%1").arg(i), i18n("Desktop %1", i));
        if (m_rootInfo) {
            m_rootInfo->setDesktopName(i, s.toUtf8().data());
        }
        m_subspaces[i - 1]->setName(s);

        auto const sId = group.readEntry(QStringLiteral("Id_%1").arg(i), QString());

        if (m_subspaces[i - 1]->id().isEmpty()) {
            m_subspaces[i - 1]->setId(sId.isEmpty() ? generateDesktopId() : sId);
        } else {
            Q_ASSERT(sId.isEmpty() || m_subspaces[i - 1]->id() == sId);
        }

        // TODO: update desktop focus chain, why?
        //         m_desktopFocusChain.value()[i-1] = i;
    }

    updateRootInfo();
    updateLayout();

    for (auto vd : qAsConst(new_desktops)) {
        Q_EMIT qobject->subspace_created(vd);
    }

    Q_EMIT qobject->countChanged(oldCount, m_subspaces.count());

    int rows = group.readEntry<int>("Rows", 2);
    m_rows = qBound(1, rows, n);

    s_loadingDesktopSettings = false;
}

void subspace_manager::save()
{
    if (s_loadingDesktopSettings) {
        return;
    }
    if (!m_config) {
        return;
    }
    KConfigGroup group(m_config, QStringLiteral("Desktops"));

    for (int i = count() + 1; group.hasKey(QStringLiteral("Id_%1").arg(i)); i++) {
        group.deleteEntry(QStringLiteral("Id_%1").arg(i));
        group.deleteEntry(QStringLiteral("Name_%1").arg(i));
    }

    group.writeEntry("Number", count());

    for (uint i = 1; i <= count(); ++i) {
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
        group.writeEntry(QStringLiteral("Id_%1").arg(i), m_subspaces[i - 1]->id());
    }

    group.writeEntry("Rows", m_rows);

    // Save to disk
    group.sync();
}

QString subspace_manager::defaultName(int desktop) const
{
    return i18n("Desktop %1", desktop);
}

void subspace_manager::setNETDesktopLayout(Qt::Orientation orientation,
                                           uint width,
                                           uint height,
                                           int startingCorner)
{
    Q_UNUSED(startingCorner); // Not really worth implementing right now.
    uint const count = m_subspaces.count();

    // Calculate valid grid size
    Q_ASSERT(width > 0 || height > 0);

    if ((width <= 0) && (height > 0)) {
        width = (count + height - 1) / height;
    } else if ((height <= 0) && (width > 0)) {
        height = (count + width - 1) / width;
    }

    while (width * height < count) {
        if (orientation == Qt::Horizontal) {
            ++width;
        } else {
            ++height;
        }
    }

    m_rows = qMax(1u, height);
    m_grid.update(QSize(width, height), orientation, m_subspaces);

    // TODO: why is there no call to m_rootInfo->setDesktopLayout?
    Q_EMIT qobject->layoutChanged(width, height);
    Q_EMIT qobject->rowsChanged(height);
}

void subspace_manager::connect_gestures()
{
    QObject::connect(m_swipeGestureReleasedX.get(), &QAction::triggered, qobject.get(), [this]() {
        // Note that if desktop wrapping is disabled and there's no desktop to left or right,
        // toLeft() and toRight() will return the current desktop.
        subspace* target = m_current;
        if (m_currentDesktopOffset.x() <= -GESTURE_SWITCH_THRESHOLD) {
            target = toLeft(m_current, isNavigationWrappingAround());
        } else if (m_currentDesktopOffset.x() >= GESTURE_SWITCH_THRESHOLD) {
            target = toRight(m_current, isNavigationWrappingAround());
        }

        // If the current desktop has not changed, consider that the gesture has been canceled.
        if (m_current != target) {
            setCurrent(target);
        } else {
            Q_EMIT qobject->current_changing_cancelled();
        }
        m_currentDesktopOffset = QPointF(0, 0);
    });
    QObject::connect(m_swipeGestureReleasedY.get(), &QAction::triggered, qobject.get(), [this]() {
        // Note that if desktop wrapping is disabled and there's no desktop above or below,
        // above() and below() will return the current desktop.
        subspace* target = m_current;
        if (m_currentDesktopOffset.y() <= -GESTURE_SWITCH_THRESHOLD) {
            target = above(m_current, isNavigationWrappingAround());
        } else if (m_currentDesktopOffset.y() >= GESTURE_SWITCH_THRESHOLD) {
            target = below(m_current, isNavigationWrappingAround());
        }

        // If the current desktop has not changed, consider that the gesture has been canceled.
        if (m_current != target) {
            setCurrent(target);
        } else {
            Q_EMIT qobject->current_changing_cancelled();
        }
        m_currentDesktopOffset = QPointF(0, 0);
    });
}

void subspace_manager::slotSwitchTo(QAction& action)
{
    bool ok = false;
    uint const i = action.data().toUInt(&ok);
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
