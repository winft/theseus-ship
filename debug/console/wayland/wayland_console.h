/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input_device_model.h"
#include "input_filter.h"
#include "model_helpers.h"
#include "surface_tree_model.h"

#include "debug/console/console.h"
#include "input/redirect_qobject.h"

namespace KWin::debug
{

class KWIN_EXPORT wayland_console_model : public console_model
{
    Q_OBJECT
public:
    ~wayland_console_model();

    template<typename Space>
    static wayland_console_model* create(Space& space, QObject* parent = nullptr)
    {
        auto model = new wayland_console_model(parent);
        model_setup_connections(*model, space);
        wayland_model_setup_connections(*model, space);
        return model;
    }

    bool get_client_count(int parent_id, int& count) const override;
    bool get_property_count(QModelIndex const& parent, int& count) const override;

    bool get_client_index(int row, int column, int parent_id, QModelIndex& index) const override;
    bool get_property_index(int row,
                            int column,
                            QModelIndex const& parent,
                            QModelIndex& index) const override;

    QVariant get_client_data(QModelIndex const& index, int role) const override;
    QVariant get_client_property_data(QModelIndex const& index, int role) const override;

    int topLevelRowCount() const override;
    win::property_window* shellClient(QModelIndex const& index) const;

    std::vector<std::unique_ptr<win::property_window>> m_shellClients;

private:
    explicit wayland_console_model(QObject* parent = nullptr);
};

class KWIN_EXPORT wayland_console_delegate : public console_delegate
{
    Q_OBJECT
public:
    explicit wayland_console_delegate(QObject* parent = nullptr);

    QString displayText(const QVariant& value, const QLocale& locale) const override;
};

template<typename Space>
class KWIN_EXPORT wayland_console : public console<Space>
{
public:
    wayland_console(Space& space)
        : console<Space>(space)
    {
        this->m_ui->windowsView->setItemDelegate(new wayland_console_delegate(this));
        this->m_ui->windowsView->setModel(wayland_console_model::create(space, this));
        this->m_ui->surfacesView->setModel(new surface_tree_model(space, this));

        auto device_model = new input_device_model(this);
        setup_input_device_model(*device_model, *space.input->platform.dbus);
        this->m_ui->inputDevicesView->setModel(device_model);
        this->m_ui->inputDevicesView->setItemDelegate(new wayland_console_delegate(this));

        QObject::connect(
            this->m_ui->tabWidget, &QTabWidget::currentChanged, this, [this, &space](int index) {
                // delay creation of input event filter until the tab is selected
                if (!m_inputFilter && index == 2) {
                    m_inputFilter = std::make_unique<input_filter<typename Space::input_t>>(
                        *space.input, this->m_ui->inputTextEdit);
                    space.input->m_spies.push_back(m_inputFilter.get());
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
        this->setWindowFlags(Qt::X11BypassWindowManagerHint);
    }

private:
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
                text.append(
                    QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(name(map, i))));
            }
        }
        text.append(QStringLiteral("</ul>"));
        return text;
    }

    void update_keyboard_tab()
    {
        auto xkb = input::xkb::get_primary_xkb_keyboard(*this->space.base.input);
        auto keymap = xkb->keymap->raw;

        this->m_ui->layoutsLabel->setText(keymapComponentToString<xkb_layout_index_t>(
            keymap, xkb_keymap_num_layouts(keymap), &xkb_keymap_layout_get_name));
        this->m_ui->currentLayoutLabel->setText(xkb_keymap_layout_get_name(keymap, xkb->layout));
        this->m_ui->modifiersLabel->setText(keymapComponentToString<xkb_mod_index_t>(
            keymap, xkb_keymap_num_mods(keymap), &xkb_keymap_mod_get_name));
        this->m_ui->ledsLabel->setText(keymapComponentToString<xkb_led_index_t>(
            keymap, xkb_keymap_num_leds(keymap), &xkb_keymap_led_get_name));
        this->m_ui->activeLedsLabel->setText(
            stateActiveComponents<xkb_led_index_t>(xkb->state,
                                                   xkb_keymap_num_leds(keymap),
                                                   &xkb_state_led_index_is_active,
                                                   &xkb_keymap_led_get_name));

        using namespace std::placeholders;
        auto modActive = std::bind(xkb_state_mod_index_is_active, _1, _2, XKB_STATE_MODS_EFFECTIVE);
        this->m_ui->activeModifiersLabel->setText(stateActiveComponents<xkb_mod_index_t>(
            xkb->state, xkb_keymap_num_mods(keymap), modActive, &xkb_keymap_mod_get_name));
    }

    std::unique_ptr<input_filter<typename Space::input_t>> m_inputFilter;
};

}
