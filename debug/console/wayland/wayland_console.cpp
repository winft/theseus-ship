/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wayland_console.h"

#include "input_device_model.h"
#include "input_filter.h"
#include "model_helpers.h"
#include "surface_tree_model.h"

#include "debug/console/model_helpers.h"
#include "debug/console/window.h"
#include "input/dbus/device_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "input/xkb/keyboard.h"
#include "input_filter.h"
#include "main.h"

#include "ui_debug_console.h"

#include <Wrapland/Server/surface.h>

namespace KWin::debug
{

wayland_console::wayland_console(wayland_space& space)
    : console(space)
{
    m_ui->windowsView->setItemDelegate(new wayland_console_delegate(this));
    m_ui->windowsView->setModel(wayland_console_model::create(space, this));
    m_ui->surfacesView->setModel(new surface_tree_model(space, this));

    auto device_model = new input_device_model(this);
    setup_input_device_model(*device_model, *space.input->platform.dbus);
    m_ui->inputDevicesView->setModel(device_model);
    m_ui->inputDevicesView->setItemDelegate(new wayland_console_delegate(this));

    QObject::connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, [this, &space](int index) {
        // delay creation of input event filter until the tab is selected
        if (!m_inputFilter && index == 2) {
            m_inputFilter = std::make_unique<input_filter>(*space.input, m_ui->inputTextEdit);
            space.input->installInputEventSpy(m_inputFilter.get());
        }
        if (index == 5) {
            update_keyboard_tab();
            QObject::connect(space.input->qobject.get(),
                             &input::redirect_qobject::keyStateChanged,
                             this,
                             &wayland_console::update_keyboard_tab);
        }
    });

    // TODO(romangg): Can we do that on Wayland differently?
    setWindowFlags(Qt::X11BypassWindowManagerHint);
}

wayland_console::~wayland_console() = default;

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

void wayland_console::update_keyboard_tab()
{
    auto xkb = input::xkb::get_primary_xkb_keyboard(space.input->platform);
    auto keymap = xkb->keymap->raw;

    m_ui->layoutsLabel->setText(keymapComponentToString<xkb_layout_index_t>(
        keymap, xkb_keymap_num_layouts(keymap), &xkb_keymap_layout_get_name));
    m_ui->currentLayoutLabel->setText(xkb_keymap_layout_get_name(keymap, xkb->layout));
    m_ui->modifiersLabel->setText(keymapComponentToString<xkb_mod_index_t>(
        keymap, xkb_keymap_num_mods(keymap), &xkb_keymap_mod_get_name));
    m_ui->ledsLabel->setText(keymapComponentToString<xkb_led_index_t>(
        keymap, xkb_keymap_num_leds(keymap), &xkb_keymap_led_get_name));
    m_ui->activeLedsLabel->setText(
        stateActiveComponents<xkb_led_index_t>(xkb->state,
                                               xkb_keymap_num_leds(keymap),
                                               &xkb_state_led_index_is_active,
                                               &xkb_keymap_led_get_name));

    using namespace std::placeholders;
    auto modActive = std::bind(xkb_state_mod_index_is_active, _1, _2, XKB_STATE_MODS_EFFECTIVE);
    m_ui->activeModifiersLabel->setText(stateActiveComponents<xkb_mod_index_t>(
        xkb->state, xkb_keymap_num_mods(keymap), modActive, &xkb_keymap_mod_get_name));
}

wayland_console_model::wayland_console_model(QObject* parent)
    : console_model(parent)
{
}

wayland_console_model::~wayland_console_model() = default;

wayland_console_model* wayland_console_model::create(win::space& space, QObject* parent)
{
    auto model = new wayland_console_model(parent);
    model_setup_connections(*model, space);
    wayland_model_setup_connections(*model, space);
    return model;
}

int wayland_console_model::topLevelRowCount() const
{
    return 4;
}

bool wayland_console_model::get_client_count(int parent_id, int& count) const
{
    if (parent_id == s_waylandClientId) {
        count = m_shellClients.size();
        return true;
    }
    return console_model::get_client_count(parent_id, count);
}

bool wayland_console_model::get_property_count(QModelIndex const& parent, int& count) const
{
    auto id = parent.internalId();

    if (id < s_idDistance * (s_x11UnmanagedId + 1)
        || id >= s_idDistance * (s_waylandClientId + 1)) {
        return console_model::get_property_count(parent, count);
    }
    count = window_property_count(this, parent, &wayland_console_model::shellClient);
    return true;
}

bool wayland_console_model::get_client_index(int row,
                                             int column,
                                             int parent_id,
                                             QModelIndex& index) const
{
    // index for a client (second level)
    if (parent_id == s_waylandClientId) {
        index = index_for_window(this, row, column, m_shellClients, s_waylandClientId);
        return true;
    }

    return console_model::get_client_index(row, column, parent_id, index);
}

bool wayland_console_model::get_property_index(int row,
                                               int column,
                                               QModelIndex const& parent,
                                               QModelIndex& index) const
{
    auto id = parent.internalId();

    if (id < s_idDistance * (s_x11UnmanagedId + 1)
        || id >= s_idDistance * (s_waylandClientId + 1)) {
        return console_model::get_property_index(row, column, parent, index);
    }

    index = index_for_property(this, row, column, parent, &wayland_console_model::shellClient);
    return true;
}

QVariant wayland_console_model::get_client_property_data(QModelIndex const& index, int role) const
{
    if (auto window = shellClient(index)) {
        return propertyData(window, index, role);
    }

    return console_model::get_client_property_data(index, role);
}

QVariant wayland_console_model::get_client_data(QModelIndex const& index, int role) const
{
    if (index.parent().internalId() == s_waylandClientId) {
        return window_data(index, role, m_shellClients);
    }

    return console_model::get_client_data(index, role);
}

console_window* wayland_console_model::shellClient(QModelIndex const& index) const
{
    return window_for_index(index, m_shellClients, s_waylandClientId);
}

wayland_console_delegate::wayland_console_delegate(QObject* parent)
    : console_delegate(parent)
{
}

QString wayland_console_delegate::displayText(const QVariant& value, const QLocale& locale) const
{
    if (value.userType() == qMetaTypeId<Wrapland::Server::Surface*>()) {
        if (auto s = value.value<Wrapland::Server::Surface*>()) {
            return QStringLiteral("Wrapland::Server::Surface(0x%1)").arg(qulonglong(s), 0, 16);
        } else {
            return QStringLiteral("nullptr");
        }
    }

    return console_delegate::displayText(value, locale);
}

}
