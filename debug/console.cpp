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

#include "input_device_model.h"
#include "input_filter.h"
#include "surface_tree_model.h"
#include "ui_debug_console.h"

#include "input/keyboard_redirect.h"
#include "main.h"
#include "render/compositor.h"
#include "scene.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/control.h"
#include "win/internal_client.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <kwinglplatform.h>
#include <kwinglutils.h>

#include <Wrapland/Server/surface.h>

#include <KLocalizedString>
#include <NETWM>

#include <QMetaProperty>
#include <QMetaType>
#include <QWindow>

#include <functional>
#include <xkbcommon/xkbcommon.h>

namespace KWin::debug
{

console::console()
    : QWidget()
    , m_ui(new Ui::debug_console)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_ui->setupUi(this);
    m_ui->windowsView->setItemDelegate(new console_delegate(this));
    m_ui->windowsView->setModel(new console_model(this));
    m_ui->surfacesView->setModel(new surface_tree_model(this));
    if (waylandServer()) {
        m_ui->inputDevicesView->setModel(new input_device_model(this));
        m_ui->inputDevicesView->setItemDelegate(new console_delegate(this));
    }
    m_ui->quitButton->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
    m_ui->tabWidget->setTabIcon(0, QIcon::fromTheme(QStringLiteral("view-list-tree")));
    m_ui->tabWidget->setTabIcon(1, QIcon::fromTheme(QStringLiteral("view-list-tree")));

    if (kwinApp()->operationMode() == Application::OperationMode::OperationModeX11) {
        m_ui->tabWidget->setTabEnabled(1, false);
        m_ui->tabWidget->setTabEnabled(2, false);
    }
    if (!waylandServer()) {
        m_ui->tabWidget->setTabEnabled(3, false);
    }

    connect(m_ui->quitButton, &QAbstractButton::clicked, this, &console::deleteLater);
    connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        // delay creation of input event filter until the tab is selected
        if (index == 2 && m_inputFilter.isNull()) {
            m_inputFilter.reset(new input_filter(m_ui->inputTextEdit));
            kwinApp()->input->redirect->installInputEventSpy(m_inputFilter.data());
        }
        if (index == 5) {
            updateKeyboardTab();
            connect(kwinApp()->input->redirect.get(),
                    &input::redirect::keyStateChanged,
                    this,
                    &console::updateKeyboardTab);
        }
    });

    // for X11
    setWindowFlags(Qt::X11BypassWindowManagerHint);

    initGLTab();
}

console::~console() = default;

void console::initGLTab()
{
    if (!effects || !effects->isOpenGLCompositing()) {
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
        extensionsString(render::compositor::self()->scene()->openGLPlatformInterfaceExtensions()));
    m_ui->openGLExtensionsLabel->setText(extensionsString(openGLExtensions()));
}

template<typename T>
QString keymapComponentToString(xkb_keymap* map,
                                const T& count,
                                std::function<const char*(xkb_keymap*, T)> f)
{
    QString text = QStringLiteral("<ul>");
    for (T i = 0; i < count; i++) {
        text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(f(map, i))));
    }
    text.append(QStringLiteral("</ul>"));
    return text;
}

template<typename T>
QString stateActiveComponents(xkb_state* state,
                              const T& count,
                              std::function<int(xkb_state*, T)> f,
                              std::function<const char*(xkb_keymap*, T)> name)
{
    QString text = QStringLiteral("<ul>");
    xkb_keymap* map = xkb_state_get_keymap(state);
    for (T i = 0; i < count; i++) {
        if (f(state, i) == 1) {
            text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(name(map, i))));
        }
    }
    text.append(QStringLiteral("</ul>"));
    return text;
}

void console::updateKeyboardTab()
{
    auto xkb = kwinApp()->input->redirect->keyboard()->xkb();
    xkb_keymap* map = xkb->keymap();
    xkb_state* state = xkb->state();
    m_ui->layoutsLabel->setText(keymapComponentToString<xkb_layout_index_t>(
        map, xkb_keymap_num_layouts(map), &xkb_keymap_layout_get_name));
    m_ui->currentLayoutLabel->setText(xkb_keymap_layout_get_name(map, xkb->currentLayout()));
    m_ui->modifiersLabel->setText(keymapComponentToString<xkb_mod_index_t>(
        map, xkb_keymap_num_mods(map), &xkb_keymap_mod_get_name));
    m_ui->ledsLabel->setText(keymapComponentToString<xkb_led_index_t>(
        map, xkb_keymap_num_leds(map), &xkb_keymap_led_get_name));
    m_ui->activeLedsLabel->setText(stateActiveComponents<xkb_led_index_t>(
        state, xkb_keymap_num_leds(map), &xkb_state_led_index_is_active, &xkb_keymap_led_get_name));

    using namespace std::placeholders;
    auto modActive = std::bind(xkb_state_mod_index_is_active, _1, _2, XKB_STATE_MODS_EFFECTIVE);
    m_ui->activeModifiersLabel->setText(stateActiveComponents<xkb_mod_index_t>(
        state, xkb_keymap_num_mods(map), modActive, &xkb_keymap_mod_get_name));
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

    if (value.userType() == qMetaTypeId<Wrapland::Server::Surface*>()) {
        if (auto s = value.value<Wrapland::Server::Surface*>()) {
            return QStringLiteral("Wrapland::Server::Surface(0x%1)").arg(qulonglong(s), 0, 16);
        } else {
            return QStringLiteral("nullptr");
        }
    }

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

static const int s_x11ClientId = 1;
static const int s_x11UnmanagedId = 2;
static const int s_waylandClientId = 3;
static const int s_workspaceInternalId = 4;
static const quint32 s_propertyBitMask = 0xFFFF0000;
static const quint32 s_clientBitMask = 0x0000FFFF;
static const quint32 s_idDistance = 10000;

template<class T>
void console_model::add(int parentRow, QVector<T*>& clients, T* client)
{
    beginInsertRows(index(parentRow, 0, QModelIndex()), clients.count(), clients.count());
    clients.append(client);
    endInsertRows();
}

template<class T>
void console_model::remove(int parentRow, QVector<T*>& clients, T* client)
{
    const int remove = clients.indexOf(client);
    if (remove == -1) {
        return;
    }
    beginRemoveRows(index(parentRow, 0, QModelIndex()), remove, remove);
    clients.removeAt(remove);
    endRemoveRows();
}

console_model::console_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    if (waylandServer()) {
        auto const clients = waylandServer()->windows;
        for (auto c : clients) {
            m_shellClients.append(c);
        }
        // TODO: that only includes windows getting shown, not those which are only created
        connect(waylandServer(), &WaylandServer::window_added, this, [this](auto win) {
            add(s_waylandClientId - 1, m_shellClients, win);
        });
        connect(waylandServer(), &WaylandServer::window_removed, this, [this](auto win) {
            remove(s_waylandClientId - 1, m_shellClients, win);
        });
    }
    for (auto const& client : workspace()->allClientList()) {
        auto x11_client = qobject_cast<win::x11::window*>(client);
        if (x11_client) {
            m_x11Clients.append(x11_client);
        }
    }
    connect(workspace(), &Workspace::clientAdded, this, [this](auto c) {
        add(s_x11ClientId - 1, m_x11Clients, c);
    });
    connect(workspace(), &Workspace::clientRemoved, this, [this](Toplevel* window) {
        auto c = qobject_cast<win::x11::window*>(window);
        if (!c) {
            return;
        }
        remove(s_x11ClientId - 1, m_x11Clients, c);
    });

    const auto unmangeds = workspace()->unmanagedList();
    for (auto u : unmangeds) {
        m_unmanageds.append(u);
    }
    connect(workspace(), &Workspace::unmanagedAdded, this, [this](Toplevel* u) {
        add(s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    connect(workspace(), &Workspace::unmanagedRemoved, this, [this](Toplevel* u) {
        remove(s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    for (auto const& window : workspace()->windows()) {
        if (auto internal = qobject_cast<win::InternalClient*>(window)) {
            m_internalClients.append(internal);
        }
    }
    connect(
        workspace(), &Workspace::internalClientAdded, this, [this](win::InternalClient* client) {
            add(s_workspaceInternalId - 1, m_internalClients, client);
        });
    connect(
        workspace(), &Workspace::internalClientRemoved, this, [this](win::InternalClient* client) {
            remove(s_workspaceInternalId - 1, m_internalClients, client);
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
    return kwinApp()->shouldUseWaylandForCompositing() ? 4 : 2;
}

template<class T>
int console_model::propertyCount(const QModelIndex& parent,
                                 T* (console_model::*filter)(const QModelIndex&) const) const
{
    if (T* t = (this->*filter)(parent)) {
        return t->metaObject()->propertyCount();
    }
    return 0;
}

int console_model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return topLevelRowCount();
    }

    switch (parent.internalId()) {
    case s_x11ClientId:
        return m_x11Clients.count();
    case s_x11UnmanagedId:
        return m_unmanageds.count();
    case s_waylandClientId:
        return m_shellClients.count();
    case s_workspaceInternalId:
        return m_internalClients.count();
    default:
        break;
    }

    if (parent.internalId() & s_propertyBitMask) {
        // properties do not have children
        return 0;
    }

    if (parent.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return propertyCount(parent, &console_model::x11Client);
    } else if (parent.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return propertyCount(parent, &console_model::unmanaged);
    } else if (parent.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return propertyCount(parent, &console_model::shellClient);
    } else if (parent.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return propertyCount(parent, &console_model::internalClient);
    }

    return 0;
}

template<class T>
QModelIndex
console_model::indexForClient(int row, int column, const QVector<T*>& clients, int id) const
{
    if (column != 0) {
        return QModelIndex();
    }
    if (row >= clients.count()) {
        return QModelIndex();
    }
    return createIndex(row, column, s_idDistance * id + row);
}

template<class T>
QModelIndex console_model::indexForProperty(int row,
                                            int column,
                                            const QModelIndex& parent,
                                            T* (console_model::*filter)(const QModelIndex&)
                                                const) const
{
    if (T* t = (this->*filter)(parent)) {
        if (row >= t->metaObject()->propertyCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, quint32(row + 1) << 16 | parent.internalId());
    }
    return QModelIndex();
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
    // index for a client (second level)
    switch (parent.internalId()) {
    case s_x11ClientId:
        return indexForClient(row, column, m_x11Clients, s_x11ClientId);
    case s_x11UnmanagedId:
        return indexForClient(row, column, m_unmanageds, s_x11UnmanagedId);
    case s_waylandClientId:
        return indexForClient(row, column, m_shellClients, s_waylandClientId);
    case s_workspaceInternalId:
        return indexForClient(row, column, m_internalClients, s_workspaceInternalId);
    default:
        break;
    }

    // index for a property (third level)
    if (parent.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return indexForProperty(row, column, parent, &console_model::x11Client);
    } else if (parent.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return indexForProperty(row, column, parent, &console_model::unmanaged);
    } else if (parent.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return indexForProperty(row, column, parent, &console_model::shellClient);
    } else if (parent.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return indexForProperty(row, column, parent, &console_model::internalClient);
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

template<class T>
QVariant
console_model::clientData(const QModelIndex& index, int role, const QVector<T*> clients) const
{
    if (index.row() >= clients.count()) {
        return QVariant();
    }
    auto c = clients.at(index.row());
    if (role == Qt::DisplayRole) {
        return QStringLiteral("%1: %2")
            .arg(static_cast<Toplevel*>(c)->xcb_window())
            .arg(win::caption(c));
    } else if (role == Qt::DecorationRole) {
        return c->control->icon();
    }
    return QVariant();
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
        if (auto c = shellClient(index)) {
            return propertyData(c, index, role);
        } else if (win::InternalClient* c = internalClient(index)) {
            return propertyData(c, index, role);
        } else if (auto c = x11Client(index)) {
            return propertyData(c, index, role);
        } else if (auto u = unmanaged(index)) {
            return propertyData(u, index, role);
        }
    } else {
        if (index.column() != 0) {
            return QVariant();
        }
        switch (index.parent().internalId()) {
        case s_x11ClientId:
            return clientData(index, role, m_x11Clients);
        case s_x11UnmanagedId: {
            if (index.row() >= m_unmanageds.count()) {
                return QVariant();
            }
            auto u = m_unmanageds.at(index.row());
            if (role == Qt::DisplayRole) {
                return u->xcb_window();
            }
            break;
        }
        case s_waylandClientId:
            return clientData(index, role, m_shellClients);
        case s_workspaceInternalId:
            return clientData(index, role, m_internalClients);
        default:
            break;
        }
    }

    return QVariant();
}

template<class T>
static T* clientForIndex(const QModelIndex& index, const QVector<T*>& clients, int id)
{
    const qint32 row = (index.internalId() & s_clientBitMask) - (s_idDistance * id);
    if (row < 0 || row >= clients.count()) {
        return nullptr;
    }
    return clients.at(row);
}

win::wayland::window* console_model::shellClient(const QModelIndex& index) const
{
    return clientForIndex(index, m_shellClients, s_waylandClientId);
}

win::InternalClient* console_model::internalClient(const QModelIndex& index) const
{
    return clientForIndex(index, m_internalClients, s_workspaceInternalId);
}

win::x11::window* console_model::x11Client(const QModelIndex& index) const
{
    return clientForIndex(index, m_x11Clients, s_x11ClientId);
}

Toplevel* console_model::unmanaged(const QModelIndex& index) const
{
    return clientForIndex(index, m_unmanageds, s_x11UnmanagedId);
}

}
