/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "console.h"

#include "model_helpers.h"
#include "ui_debug_console.h"

#include "render/compositor.h"
#include "render/effects.h"
#include "render/scene.h"
#include "win/internal_window.h"
#include "win/space.h"
#include "win/x11/stacking.h"
#include "win/x11/window.h"

#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KLocalizedString>
#include <QMetaProperty>
#include <QMetaType>
#include <QWindow>

#include <functional>
#include <xkbcommon/xkbcommon.h>

namespace KWin::debug
{

console::console(win::space& space)
    : QWidget()
    , m_ui(new Ui::debug_console)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_ui->setupUi(this);

    m_ui->quitButton->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
    m_ui->tabWidget->setTabIcon(0, QIcon::fromTheme(QStringLiteral("view-list-tree")));
    m_ui->tabWidget->setTabIcon(1, QIcon::fromTheme(QStringLiteral("view-list-tree")));

    connect(m_ui->quitButton, &QAbstractButton::clicked, this, &console::deleteLater);

    initGLTab(*space.render.scene);
}

console::~console() = default;

void console::initGLTab(render::scene& scene)
{
    if (!scene.compositor.effects || !scene.compositor.effects->isOpenGLCompositing()) {
        m_ui->noOpenGLLabel->setVisible(true);
        m_ui->glInfoScrollArea->setVisible(false);
        return;
    }
    GLPlatform* gl = GLPlatform::instance();
    m_ui->noOpenGLLabel->setVisible(false);
    m_ui->glInfoScrollArea->setVisible(true);
    m_ui->glVendorStringLabel->setText(QString::fromLocal8Bit(gl->glVendorString()));
    m_ui->glRendererStringLabel->setText(QString::fromLocal8Bit(gl->glRendererString()));
    m_ui->glVersionStringLabel->setText(QString::fromLocal8Bit(gl->glVersionString()));
    m_ui->glslVersionStringLabel->setText(
        QString::fromLocal8Bit(gl->glShadingLanguageVersionString()));
    m_ui->glDriverLabel->setText(GLPlatform::driverToString(gl->driver()));
    m_ui->glGPULabel->setText(GLPlatform::chipClassToString(gl->chipClass()));
    m_ui->glVersionLabel->setText(GLPlatform::versionToString(gl->glVersion()));
    m_ui->glslLabel->setText(GLPlatform::versionToString(gl->glslVersion()));

    auto extensionsString = [](const auto& extensions) {
        QString text = QStringLiteral("<ul>");
        for (auto extension : extensions) {
            text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(extension)));
        }
        text.append(QStringLiteral("</ul>"));
        return text;
    };

    m_ui->platformExtensionsLabel->setText(
        extensionsString(scene.openGLPlatformInterfaceExtensions()));
    m_ui->openGLExtensionsLabel->setText(extensionsString(openGLExtensions()));
}

void console::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // delay the connection to the show event as in ctor the windowHandle returns null
    connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible) {
            // ignore
            return;
        }
        deleteLater();
    });
}

console_delegate::console_delegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

console_delegate::~console_delegate() = default;

QString console_delegate::displayText(const QVariant& value, const QLocale& locale) const
{
    switch (value.type()) {
    case QMetaType::QPoint: {
        const QPoint p = value.toPoint();
        return QStringLiteral("%1,%2").arg(p.x()).arg(p.y());
    }
    case QMetaType::QPointF: {
        const QPointF p = value.toPointF();
        return QStringLiteral("%1,%2").arg(p.x()).arg(p.y());
    }
    case QMetaType::QSize: {
        const QSize s = value.toSize();
        return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
    }
    case QMetaType::QSizeF: {
        const QSizeF s = value.toSizeF();
        return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
    }
    case QMetaType::QRect: {
        const QRect r = value.toRect();
        return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    default:
        break;
    };

    if (value.userType() == qMetaTypeId<Qt::MouseButtons>()) {
        const auto buttons = value.value<Qt::MouseButtons>();
        if (buttons == Qt::NoButton) {
            return i18n("No Mouse Buttons");
        }
        QStringList list;
        if (buttons.testFlag(Qt::LeftButton)) {
            list << i18nc("Mouse Button", "left");
        }
        if (buttons.testFlag(Qt::RightButton)) {
            list << i18nc("Mouse Button", "right");
        }
        if (buttons.testFlag(Qt::MiddleButton)) {
            list << i18nc("Mouse Button", "middle");
        }
        if (buttons.testFlag(Qt::BackButton)) {
            list << i18nc("Mouse Button", "back");
        }
        if (buttons.testFlag(Qt::ForwardButton)) {
            list << i18nc("Mouse Button", "forward");
        }
        if (buttons.testFlag(Qt::ExtraButton1)) {
            list << i18nc("Mouse Button", "extra 1");
        }
        if (buttons.testFlag(Qt::ExtraButton2)) {
            list << i18nc("Mouse Button", "extra 2");
        }
        if (buttons.testFlag(Qt::ExtraButton3)) {
            list << i18nc("Mouse Button", "extra 3");
        }
        if (buttons.testFlag(Qt::ExtraButton4)) {
            list << i18nc("Mouse Button", "extra 4");
        }
        if (buttons.testFlag(Qt::ExtraButton5)) {
            list << i18nc("Mouse Button", "extra 5");
        }
        if (buttons.testFlag(Qt::ExtraButton6)) {
            list << i18nc("Mouse Button", "extra 6");
        }
        if (buttons.testFlag(Qt::ExtraButton7)) {
            list << i18nc("Mouse Button", "extra 7");
        }
        if (buttons.testFlag(Qt::ExtraButton8)) {
            list << i18nc("Mouse Button", "extra 8");
        }
        if (buttons.testFlag(Qt::ExtraButton9)) {
            list << i18nc("Mouse Button", "extra 9");
        }
        if (buttons.testFlag(Qt::ExtraButton10)) {
            list << i18nc("Mouse Button", "extra 10");
        }
        if (buttons.testFlag(Qt::ExtraButton11)) {
            list << i18nc("Mouse Button", "extra 11");
        }
        if (buttons.testFlag(Qt::ExtraButton12)) {
            list << i18nc("Mouse Button", "extra 12");
        }
        if (buttons.testFlag(Qt::ExtraButton13)) {
            list << i18nc("Mouse Button", "extra 13");
        }
        if (buttons.testFlag(Qt::ExtraButton14)) {
            list << i18nc("Mouse Button", "extra 14");
        }
        if (buttons.testFlag(Qt::ExtraButton15)) {
            list << i18nc("Mouse Button", "extra 15");
        }
        if (buttons.testFlag(Qt::ExtraButton16)) {
            list << i18nc("Mouse Button", "extra 16");
        }
        if (buttons.testFlag(Qt::ExtraButton17)) {
            list << i18nc("Mouse Button", "extra 17");
        }
        if (buttons.testFlag(Qt::ExtraButton18)) {
            list << i18nc("Mouse Button", "extra 18");
        }
        if (buttons.testFlag(Qt::ExtraButton19)) {
            list << i18nc("Mouse Button", "extra 19");
        }
        if (buttons.testFlag(Qt::ExtraButton20)) {
            list << i18nc("Mouse Button", "extra 20");
        }
        if (buttons.testFlag(Qt::ExtraButton21)) {
            list << i18nc("Mouse Button", "extra 21");
        }
        if (buttons.testFlag(Qt::ExtraButton22)) {
            list << i18nc("Mouse Button", "extra 22");
        }
        if (buttons.testFlag(Qt::ExtraButton23)) {
            list << i18nc("Mouse Button", "extra 23");
        }
        if (buttons.testFlag(Qt::ExtraButton24)) {
            list << i18nc("Mouse Button", "extra 24");
        }
        if (buttons.testFlag(Qt::TaskButton)) {
            list << i18nc("Mouse Button", "task");
        }
        return list.join(QStringLiteral(", "));
    }

    return QStyledItemDelegate::displayText(value, locale);
}

console_model::console_model(win::space& space, QObject* parent)
    : QAbstractItemModel(parent)
    , space{space}
{
    for (auto const& window : space.m_windows) {
        if (window->control) {
            if (auto x11_client = qobject_cast<win::x11::window*>(window)) {
                m_x11Clients.append(x11_client);
            }
        }
    }
    connect(space.qobject.get(), &win::space_qobject::clientAdded, this, [this](auto c) {
        add_window(this, s_x11ClientId - 1, m_x11Clients, c);
    });
    connect(
        space.qobject.get(), &win::space_qobject::clientRemoved, this, [this](Toplevel* window) {
            auto c = qobject_cast<win::x11::window*>(window);
            if (!c) {
                return;
            }
            remove_window(this, s_x11ClientId - 1, m_x11Clients, c);
        });

    for (auto unmanaged : win::x11::get_unmanageds<Toplevel>(space)) {
        m_unmanageds.append(unmanaged);
    }

    connect(space.qobject.get(), &win::space_qobject::unmanagedAdded, this, [this](Toplevel* u) {
        add_window(this, s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    connect(space.qobject.get(), &win::space_qobject::unmanagedRemoved, this, [this](Toplevel* u) {
        remove_window(this, s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    for (auto const& window : space.windows()) {
        if (auto internal = qobject_cast<win::internal_window*>(window)) {
            m_internalClients.append(internal);
        }
    }
    connect(space.qobject.get(),
            &win::space_qobject::internalClientAdded,
            this,
            [this](win::internal_window* client) {
                add_window(this, s_workspaceInternalId - 1, m_internalClients, client);
            });
    connect(space.qobject.get(),
            &win::space_qobject::internalClientRemoved,
            this,
            [this](win::internal_window* client) {
                remove_window(this, s_workspaceInternalId - 1, m_internalClients, client);
            });
}

console_model::~console_model() = default;

int console_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 2;
}

int console_model::topLevelRowCount() const
{
    return 2;
}

bool console_model::get_client_count(int parent_id, int& count) const
{
    switch (parent_id) {
    case s_x11ClientId:
        count = m_x11Clients.count();
        break;
    case s_x11UnmanagedId:
        count = m_unmanageds.count();
        break;
    case s_workspaceInternalId:
        count = m_internalClients.count();
        break;
    default:
        return false;
    }
    return true;
}

bool console_model::get_property_count(QModelIndex const& parent, int& count) const
{
    auto id = parent.internalId();

    if (id < s_idDistance * (s_x11ClientId + 1)) {
        count = window_property_count(this, parent, &console_model::x11Client);
        return true;
    }
    if (id < s_idDistance * (s_x11UnmanagedId + 1)) {
        count = window_property_count(this, parent, &console_model::unmanaged);
        return true;
    }
    if (id < s_idDistance * (s_workspaceInternalId + 1)) {
        count = window_property_count(this, parent, &console_model::internalClient);
        return true;
    }
    return false;
}

int console_model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return topLevelRowCount();
    }

    if (int count; get_client_count(parent.internalId(), count)) {
        return count;
    }

    if (parent.internalId() & s_propertyBitMask) {
        // properties do not have children
        return 0;
    }

    if (int count; get_property_count(parent, count)) {
        return count;
    }

    return 0;
}

bool console_model::get_client_index(int row, int column, int parent_id, QModelIndex& index) const
{
    // index for a client (second level)
    switch (parent_id) {
    case s_x11ClientId:
        index = index_for_window(this, row, column, m_x11Clients, s_x11ClientId);
        break;
    case s_x11UnmanagedId:
        index = index_for_window(this, row, column, m_unmanageds, s_x11UnmanagedId);
        break;
    case s_workspaceInternalId:
        index = index_for_window(this, row, column, m_internalClients, s_workspaceInternalId);
        break;
    default:
        return false;
    }

    return true;
}

bool console_model::get_property_index(int row,
                                       int column,
                                       QModelIndex const& parent,
                                       QModelIndex& index) const
{
    // index for a property (third level)
    if (parent.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        index = index_for_property(this, row, column, parent, &console_model::x11Client);
        return true;
    }
    if (parent.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        index = index_for_property(this, row, column, parent, &console_model::unmanaged);
        return true;
    }
    if (parent.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        index = index_for_property(this, row, column, parent, &console_model::internalClient);
        return true;
    }
    return false;
}

QModelIndex console_model::index(int row, int column, const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        // index for a top level item
        if (column != 0 || row >= topLevelRowCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, row + 1);
    }

    if (column >= 2) {
        // max of 2 columns
        return QModelIndex();
    }

    if (QModelIndex index; get_client_index(row, column, parent.internalId(), index)) {
        return index;
    }

    if (QModelIndex index; get_property_index(row, column, parent, index)) {
        return index;
    }

    return QModelIndex();
}

QModelIndex console_model::parent(const QModelIndex& child) const
{
    if (child.internalId() <= s_workspaceInternalId) {
        return QModelIndex();
    }

    if (child.internalId() & s_propertyBitMask) {
        // a property
        const quint32 parentId = child.internalId() & s_clientBitMask;
        if (parentId < s_idDistance * (s_x11ClientId + 1)) {
            return createIndex(parentId - (s_idDistance * s_x11ClientId), 0, parentId);
        } else if (parentId < s_idDistance * (s_x11UnmanagedId + 1)) {
            return createIndex(parentId - (s_idDistance * s_x11UnmanagedId), 0, parentId);
        } else if (parentId < s_idDistance * (s_waylandClientId + 1)) {
            return createIndex(parentId - (s_idDistance * s_waylandClientId), 0, parentId);
        } else if (parentId < s_idDistance * (s_workspaceInternalId + 1)) {
            return createIndex(parentId - (s_idDistance * s_workspaceInternalId), 0, parentId);
        }
        return QModelIndex();
    }

    if (child.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return createIndex(s_x11ClientId - 1, 0, s_x11ClientId);
    } else if (child.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return createIndex(s_x11UnmanagedId - 1, 0, s_x11UnmanagedId);
    } else if (child.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return createIndex(s_waylandClientId - 1, 0, s_waylandClientId);
    } else if (child.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return createIndex(s_workspaceInternalId - 1, 0, s_workspaceInternalId);
    }

    return QModelIndex();
}

QModelIndex console_model::create_index(int row, int column, quintptr id) const
{
    return createIndex(row, column, id);
}

void console_model::begin_insert_rows(QModelIndex const& parent, int first, int last)
{
    beginInsertRows(parent, first, last);
}

void console_model::end_insert_rows()
{
    endInsertRows();
}

void console_model::begin_remove_rows(QModelIndex const& parent, int first, int last)
{
    beginRemoveRows(parent, first, last);
}

void console_model::end_remove_rows()
{
    endRemoveRows();
}

QVariant console_model::propertyData(QObject* object, const QModelIndex& index, int role) const
{
    Q_UNUSED(role)
    const auto property = object->metaObject()->property(index.row());
    if (index.column() == 0) {
        return property.name();
    } else {
        const QVariant value = property.read(object);
        if (qstrcmp(property.name(), "windowType") == 0) {
            switch (value.toInt()) {
            case NET::Normal:
                return QStringLiteral("NET::Normal");
            case NET::Desktop:
                return QStringLiteral("NET::Desktop");
            case NET::Dock:
                return QStringLiteral("NET::Dock");
            case NET::Toolbar:
                return QStringLiteral("NET::Toolbar");
            case NET::Menu:
                return QStringLiteral("NET::Menu");
            case NET::Dialog:
                return QStringLiteral("NET::Dialog");
            case NET::Override:
                return QStringLiteral("NET::Override");
            case NET::TopMenu:
                return QStringLiteral("NET::TopMenu");
            case NET::Utility:
                return QStringLiteral("NET::Utility");
            case NET::Splash:
                return QStringLiteral("NET::Splash");
            case NET::DropdownMenu:
                return QStringLiteral("NET::DropdownMenu");
            case NET::PopupMenu:
                return QStringLiteral("NET::PopupMenu");
            case NET::Tooltip:
                return QStringLiteral("NET::Tooltip");
            case NET::Notification:
                return QStringLiteral("NET::Notification");
            case NET::ComboBox:
                return QStringLiteral("NET::ComboBox");
            case NET::DNDIcon:
                return QStringLiteral("NET::DNDIcon");
            case NET::OnScreenDisplay:
                return QStringLiteral("NET::OnScreenDisplay");
            case NET::CriticalNotification:
                return QStringLiteral("NET::CriticalNotification");
            case NET::Unknown:
            default:
                return QStringLiteral("NET::Unknown");
            }
        }
        return value;
    }
    return QVariant();
}

QVariant console_model::get_client_property_data(QModelIndex const& index, int role) const
{
    if (auto c = internalClient(index)) {
        return propertyData(c, index, role);
    }
    if (auto c = x11Client(index)) {
        return propertyData(c, index, role);
    }
    if (auto u = unmanaged(index)) {
        return propertyData(u, index, role);
    }
    return QVariant();
}

QVariant console_model::get_client_data(QModelIndex const& index, int role) const
{
    switch (index.parent().internalId()) {
    case s_x11ClientId:
        return window_data(index, role, m_x11Clients);
    case s_x11UnmanagedId: {
        if (index.row() >= m_unmanageds.count()) {
            return QVariant();
        }
        auto u = m_unmanageds.at(index.row());
        if (role == Qt::DisplayRole) {
            return static_cast<xcb_window_t>(u->xcb_window);
        }
        return QVariant();
    }
    case s_workspaceInternalId:
        return window_data(index, role, m_internalClients);
    default:
        return QVariant();
    }
}

QVariant console_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (!index.parent().isValid()) {
        // one of the top levels
        if (index.column() != 0 || role != Qt::DisplayRole) {
            return QVariant();
        }
        switch (index.internalId()) {
        case s_x11ClientId:
            return i18n("X11 Client Windows");
        case s_x11UnmanagedId:
            return i18n("X11 Unmanaged Windows");
        case s_waylandClientId:
            return i18n("Wayland Windows");
        case s_workspaceInternalId:
            return i18n("Internal Windows");
        default:
            return QVariant();
        }
    }

    if (index.internalId() & s_propertyBitMask) {
        if (index.column() >= 2 || role != Qt::DisplayRole) {
            return QVariant();
        }
        return get_client_property_data(index, role);
    }

    if (index.column() != 0) {
        return QVariant();
    }

    return get_client_data(index, role);
}

win::internal_window* console_model::internalClient(const QModelIndex& index) const
{
    return window_for_index(index, m_internalClients, s_workspaceInternalId);
}

win::x11::window* console_model::x11Client(const QModelIndex& index) const
{
    return window_for_index(index, m_x11Clients, s_x11ClientId);
}

Toplevel* console_model::unmanaged(const QModelIndex& index) const
{
    return window_for_index(index, m_unmanageds, s_x11UnmanagedId);
}

}
