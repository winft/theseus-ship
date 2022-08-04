/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_layouts_v2.h"

#include "input/event.h"
#include "input/platform.h"
#include "input/xkb/helpers.h"
#include "input/xkb/layout_manager.h"
#include "input/xkb/layout_policies.h"
#include "main.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>

namespace KWin::input::dbus
{

constexpr auto dbus_object_path{"/LayoutsV2"};

uint keyboard_index{0};

keyboard_layouts_v2::keyboard_layouts_v2(input::platform* platform, xkb::layout_manager* parent)
    : QObject(parent)
    , layout_manager(parent)
{
    qRegisterMetaType<QVector<layout_names_v2>>("QVector<layout_names_v2>");
    qDBusRegisterMetaType<layout_names_v2>();
    qDBusRegisterMetaType<QVector<layout_names_v2>>();

    qRegisterMetaType<keyboard_v2>("keyboard_v2");
    qRegisterMetaType<QVector<keyboard_v2>>("QVector<keyboard_v2>");
    qDBusRegisterMetaType<keyboard_v2>();
    qDBusRegisterMetaType<QVector<keyboard_v2>>();

    QObject::connect(platform->qobject.get(),
                     &input::platform_qobject::keyboard_added,
                     this,
                     &keyboard_layouts_v2::handle_keyboard_added);
    QObject::connect(platform->qobject.get(),
                     &input::platform_qobject::keyboard_removed,
                     this,
                     &keyboard_layouts_v2::handle_keyboard_removed);

    for (auto& keyboard : platform->keyboards) {
        handle_keyboard_added(keyboard);
    }

    QDBusConnection::sessionBus().registerObject(dbus_object_path,
                                                 this,
                                                 QDBusConnection::ExportAllSlots
                                                     | QDBusConnection::ExportAllSignals);
}

QVector<keyboard_v2> keyboard_layouts_v2::getKeyboards() const
{
    QVector<keyboard_v2> ret;
    for (auto const& [index, keyboard] : keyboards) {
        ret.push_back(keyboard.data);
    }
    return ret;
}

void keyboard_layouts_v2::switchToNextLayout(uint keyboard)
{
    if (auto keyboard_internal = get_internal_keyboard(keyboard)) {
        keyboard_internal->internal->xkb->switch_to_next_layout();
    }
}

void keyboard_layouts_v2::switchToPreviousLayout(uint keyboard)
{
    if (auto keyboard_internal = get_internal_keyboard(keyboard)) {
        keyboard_internal->internal->xkb->switch_to_previous_layout();
    }
}

bool keyboard_layouts_v2::setLayout(uint keyboard, uint layout)
{
    auto keyboard_internal = get_internal_keyboard(keyboard);
    if (!keyboard_internal) {
        return false;
    }

    return keyboard_internal->internal->xkb->switch_to_layout(layout);
}

uint keyboard_layouts_v2::getLayout(uint keyboard) const
{
    auto keyboard_internal = get_internal_keyboard(keyboard);
    if (!keyboard_internal) {
        return 0;
    }
    return keyboard_internal->internal->xkb->layout;
}

QVector<layout_names_v2> keyboard_layouts_v2::getLayoutsList(uint keyboard) const
{
    auto keyboard_internal = get_internal_keyboard(keyboard);
    if (!keyboard_internal) {
        return {};
    }

    auto& xkb = keyboard_internal->internal->xkb;

    QVector<layout_names_v2> ret;
    auto const layouts_count = xkb->layouts_count();

    for (size_t i = 0; i < layouts_count; ++i) {
        ret.push_back({QString::fromStdString(xkb->layout_short_name_from_index(i)),
                       xkb::translated_keyboard_layout(xkb->layout_name_from_index(i))});
    }
    return ret;
}

void keyboard_layouts_v2::handle_keyboard_added(input::keyboard* keyboard)
{
    auto& ctrl = keyboard->control;
    if (!ctrl || !ctrl->is_alpha_numeric_keyboard()) {
        return;
    }

    auto xkb = keyboard->xkb.get();
    auto const index = keyboard_index++;

    QObject::connect(xkb, &xkb::keyboard::layout_changed, this, [this, index] {
        auto const& keyboard = keyboards.at(index);
        Q_EMIT layoutChanged(keyboard.data.id, keyboard.internal->xkb->layout);
    });
    QObject::connect(xkb, &xkb::keyboard::layouts_changed, this, [this, index] {
        Q_EMIT layoutListChanged(keyboards.at(index).data.id);
    });

    auto internal = keyboard_v2_internal({index,
                                          QString::fromStdString(ctrl->metadata.name),
                                          QString::fromStdString(ctrl->metadata.sys_name),
                                          ctrl->metadata.vendor_id,
                                          ctrl->metadata.product_id,
                                          keyboard});

    keyboards.insert({index, internal});
    Q_EMIT keyboardAdded(internal.data);
}

void keyboard_layouts_v2::handle_keyboard_removed(input::keyboard* keyboard)
{
    auto it = std::find_if(keyboards.begin(), keyboards.end(), [keyboard](auto const& element) {
        return element.second.internal == keyboard;
    });
    if (it != keyboards.end()) {
        auto id = it->second.data.id;
        keyboards.erase(it);
        Q_EMIT keyboardRemoved(id);
    }
}

keyboard_v2_internal const* keyboard_layouts_v2::get_internal_keyboard(uint keyboard) const
{
    auto it = keyboards.find(keyboard);
    if (it == keyboards.end()) {
        return nullptr;
    }
    return &(it->second);
}

QDBusArgument& operator<<(QDBusArgument& argument, keyboard_v2 const& keyboard)
{
    argument.beginStructure();
    argument << keyboard.id << keyboard.name << keyboard.sys_name << keyboard.vendor_id
             << keyboard.product_id;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(QDBusArgument const& argument, keyboard_v2& keyboard)
{
    argument.beginStructure();
    argument >> keyboard.id >> keyboard.name >> keyboard.sys_name >> keyboard.vendor_id
        >> keyboard.product_id;
    argument.endStructure();
    return argument;
}

QDBusArgument& operator<<(QDBusArgument& argument, layout_names_v2 const& names)
{
    argument.beginStructure();
    argument << names.short_name << names.long_name;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(QDBusArgument const& argument, layout_names_v2& names)
{
    argument.beginStructure();
    argument >> names.short_name >> names.long_name;
    argument.endStructure();
    return argument;
}

}
