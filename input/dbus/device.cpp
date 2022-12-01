/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device.h"

#include <QDBusConnection>

namespace KWin::input::dbus
{

void init_device(device* dev, std::string sys_name)
{
    dev->sys_name = sys_name;
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/InputDevice/")
                                                     + sys_name.c_str(),
                                                 QStringLiteral("org.kde.KWin.InputDevice"),
                                                 dev,
                                                 QDBusConnection::ExportAllProperties);
}

device::device(input::control::keyboard* control, QObject* parent)
    : QObject(parent)
    , keyboard_ctrl{control}
{
    dev = control;
    init_device(this, control->metadata.sys_name);
}

device::device(input::control::pointer* control, QObject* parent)
    : QObject(parent)
    , pointer_ctrl{control}
{
    dev = control;
    init_device(this, control->metadata.sys_name);
}

device::device(input::control::switch_device* control, QObject* parent)
    : QObject(parent)
    , switch_ctrl{control}
{
    dev = control;
    init_device(this, control->metadata.sys_name);
}

device::device(input::control::touch* control, QObject* parent)
    : QObject(parent)
    , touch_ctrl{control}
{
    dev = control;
    init_device(this, control->metadata.sys_name);
}

device::~device()
{
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/kde/KWin/InputDevice/")
                                                   + sys_name.c_str());
}

bool device::isKeyboard() const
{
    return keyboard_ctrl;
}

bool device::isAlphaNumericKeyboard() const
{
    return keyboard_ctrl && keyboard_ctrl->is_alpha_numeric_keyboard();
}

bool device::isPointer() const
{
    return pointer_ctrl && pointer_ctrl->supported_buttons() != Qt::NoButton;
}

bool device::isTouchpad() const
{
    return pointer_ctrl && pointer_ctrl->is_touchpad();
    ;
}

bool device::isTouch() const
{
    return touch_ctrl;
}

bool device::isTabletTool() const
{
    // TODO(romangg): implement once we support the device class in the backend.
    return false;
}

bool device::isTabletPad() const
{
    // TODO(romangg): implement once we support the device class in the backend.
    return false;
}

bool device::supportsGesture() const
{
    return pointer_ctrl && pointer_ctrl->supports_gesture();
}

QString device::name() const
{
    return dev->metadata.name.c_str();
}

QString device::sysName() const
{
    return dev->metadata.sys_name.c_str();
}

QString device::outputName() const
{
    return touch_ctrl ? touch_ctrl->output_name().c_str() : QString();
}

QSizeF device::size() const
{
    if (pointer_ctrl) {
        return pointer_ctrl->size();
    }
    if (touch_ctrl) {
        return touch_ctrl->size();
    }
    return QSizeF();
}

quint32 device::product() const
{
    return dev->metadata.product_id;
}

quint32 device::vendor() const
{
    return dev->metadata.vendor_id;
}

Qt::MouseButtons device::supportedButtons() const
{
    return pointer_ctrl ? pointer_ctrl->supported_buttons() : Qt::MouseButtons();
}

int device::tapFingerCount() const
{
    return pointer_ctrl ? pointer_ctrl->tap_finger_count() : 0;
}

bool device::tapToClickEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->tap_to_click_enabled_by_default() : false;
}

bool device::isTapToClick() const
{
    return pointer_ctrl ? pointer_ctrl->is_tap_to_click() : false;
}

void device::setTapToClick(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_tap_to_click(set);
    }
}

bool device::tapAndDragEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->tap_and_drag_enabled_by_default() : false;
}

bool device::isTapAndDrag() const
{
    return pointer_ctrl ? pointer_ctrl->is_tap_and_drag() : false;
}

void device::setTapAndDrag(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_tap_and_drag(set);
    }
}

bool device::tapDragLockEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->tap_drag_lock_enabled_by_default() : false;
}

bool device::isTapDragLock() const
{
    return pointer_ctrl ? pointer_ctrl->is_tap_drag_lock() : false;
}

void device::setTapDragLock(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_tap_drag_lock(set);
    }
}

bool device::supportsDisableWhileTyping() const
{
    return pointer_ctrl ? pointer_ctrl->supports_disable_while_typing() : false;
}

bool device::disableWhileTypingEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->disable_while_typing_enabled_by_default() : false;
}

bool device::supportsPointerAcceleration() const
{
    return pointer_ctrl ? pointer_ctrl->supports_acceleration() : false;
}

bool device::supportsLeftHanded() const
{
    return pointer_ctrl ? pointer_ctrl->supports_left_handed() : false;
}

bool device::supportsCalibrationMatrix() const
{
    return touch_ctrl ? touch_ctrl->supports_calibration_matrix() : false;
}

bool device::supportsDisableEvents() const
{
    return dev->supports_disable_events();
}

bool device::supportsDisableEventsOnExternalMouse() const
{
    return pointer_ctrl ? pointer_ctrl->supports_disable_events_on_external_mouse() : false;
}

bool device::supportsMiddleEmulation() const
{
    return pointer_ctrl ? pointer_ctrl->supports_middle_emulation() : false;
}

bool device::middleEmulationEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->middle_emulation_enabled_by_default() : false;
}

bool device::isMiddleEmulation() const
{
    return pointer_ctrl ? pointer_ctrl->is_middle_emulation() : false;
}

bool device::supportsNaturalScroll() const
{
    return pointer_ctrl ? pointer_ctrl->supports_natural_scroll() : false;
}

bool device::supportsScrollTwoFinger() const
{
    return pointer_ctrl ? pointer_ctrl->supports_scroll_method(control::scroll::two_finger) : false;
}

bool device::supportsScrollEdge() const
{
    return pointer_ctrl ? pointer_ctrl->supports_scroll_method(control::scroll::edge) : false;
}

bool device::supportsScrollOnButtonDown() const
{
    return pointer_ctrl ? pointer_ctrl->supports_scroll_method(control::scroll::on_button_down)
                        : false;
}

bool device::leftHandedEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->supports_left_handed() : false;
}

bool device::naturalScrollEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->natural_scroll_enabled_by_default() : false;
}

bool device::scrollTwoFingerEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->default_scroll_method() == control::scroll::two_finger
                        : false;
}

bool device::scrollEdgeEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->default_scroll_method() == control::scroll::edge : false;
}

bool device::scrollOnButtonDownEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->default_scroll_method() == control::scroll::on_button_down
                        : false;
}

bool device::supportsLmrTapButtonMap() const
{
    return pointer_ctrl ? pointer_ctrl->supports_lmr_tap_button_map() : false;
}

bool device::lmrTapButtonMapEnabledByDefault() const
{
    return pointer_ctrl ? pointer_ctrl->lmr_tap_button_map_enabled_by_default() : false;
}

void device::setLmrTapButtonMap(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_lmr_tap_button_map(set);
    }
}

bool device::lmrTapButtonMap() const
{
    return pointer_ctrl ? pointer_ctrl->lmr_tap_button_map() : false;
}

quint32 device::defaultScrollButton() const
{
    return pointer_ctrl ? pointer_ctrl->default_scroll_button() : 0;
}

void device::setMiddleEmulation(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_middle_emulation(set);
    }
}

bool device::isNaturalScroll() const
{
    return pointer_ctrl ? pointer_ctrl->is_natural_scroll() : false;
}

void device::setNaturalScroll(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_natural_scroll(set);
    }
}

bool device::isScrollTwoFinger() const
{
    return pointer_ctrl ? pointer_ctrl->scroll_method() == control::scroll::two_finger : false;
}

void device::setScrollTwoFinger(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_scroll_method(control::scroll::two_finger);
    } else if (pointer_ctrl->scroll_method() == control::scroll::two_finger) {
        pointer_ctrl->set_scroll_method(control::scroll::none);
    }
}
bool device::isScrollEdge() const
{
    return pointer_ctrl ? pointer_ctrl->scroll_method() == control::scroll::edge : false;
}

void device::setScrollEdge(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_scroll_method(control::scroll::edge);
    } else if (pointer_ctrl->scroll_method() == control::scroll::edge) {
        pointer_ctrl->set_scroll_method(control::scroll::none);
    }
}

bool device::isScrollOnButtonDown() const
{
    return pointer_ctrl ? pointer_ctrl->scroll_method() == control::scroll::on_button_down : false;
}

void device::setScrollOnButtonDown(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_scroll_method(control::scroll::on_button_down);
    } else if (pointer_ctrl->scroll_method() == control::scroll::on_button_down) {
        pointer_ctrl->set_scroll_method(control::scroll::none);
    }
}

quint32 device::scrollButton() const
{
    return pointer_ctrl ? pointer_ctrl->scroll_button() : 0;
}

void device::setScrollButton(quint32 button)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_scroll_button(button);
    }
}

qreal device::scrollFactorDefault() const
{
    return 1.0;
}

qreal device::scrollFactor() const
{
    return pointer_ctrl ? pointer_ctrl->get_scroll_factor() : 1.;
}

void device::setScrollFactor(qreal factor)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_scroll_factor(factor);
    }
}

void device::setDisableWhileTyping(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_disable_while_typing(set);
    }
}

bool device::isDisableWhileTyping() const
{
    return pointer_ctrl ? pointer_ctrl->is_disable_while_typing() : false;
}

bool device::isLeftHanded() const
{
    return pointer_ctrl ? pointer_ctrl->is_left_handed() : false;
}

void device::setLeftHanded(bool set)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_left_handed(set);
    }
}

qreal device::defaultPointerAcceleration() const
{
    return pointer_ctrl ? pointer_ctrl->default_acceleration() : 0.;
}

qreal device::pointerAcceleration() const
{
    return pointer_ctrl ? pointer_ctrl->acceleration() : 0.;
}

void device::setPointerAcceleration(qreal acceleration)
{
    if (pointer_ctrl) {
        pointer_ctrl->set_acceleration(acceleration);
    }
}

QString acceleration_to_string(double accel)
{
    return QString::number(accel, 'f', 3);
}

bool device::supportsPointerAccelerationProfileFlat() const
{
    return pointer_ctrl ? pointer_ctrl->supports_acceleration_profile(control::accel_profile::flat)
                        : false;
}

bool device::supportsPointerAccelerationProfileAdaptive() const
{
    return pointer_ctrl
        ? pointer_ctrl->supports_acceleration_profile(control::accel_profile::adaptive)
        : false;
}

bool device::defaultPointerAccelerationProfileFlat() const
{
    return pointer_ctrl
        ? pointer_ctrl->default_acceleration_profile() == control::accel_profile::flat
        : false;
}

bool device::defaultPointerAccelerationProfileAdaptive() const
{
    return pointer_ctrl
        ? pointer_ctrl->default_acceleration_profile() == control::accel_profile::adaptive
        : false;
}

bool device::pointerAccelerationProfileFlat() const
{
    return pointer_ctrl ? pointer_ctrl->acceleration_profile() == control::accel_profile::flat
                        : false;
}

bool device::pointerAccelerationProfileAdaptive() const
{
    return pointer_ctrl ? pointer_ctrl->acceleration_profile() == control::accel_profile::adaptive
                        : false;
}

void device::setPointerAccelerationProfileFlat(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_acceleration_profile(control::accel_profile::flat);
    } else if (pointer_ctrl->acceleration_profile() == control::accel_profile::flat) {
        pointer_ctrl->set_acceleration_profile(control::accel_profile::none);
    }
}

void device::setPointerAccelerationProfileAdaptive(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_acceleration_profile(control::accel_profile::adaptive);
    } else if (pointer_ctrl->acceleration_profile() == control::accel_profile::adaptive) {
        pointer_ctrl->set_acceleration_profile(control::accel_profile::none);
    }
}

bool device::supportsClickMethodAreas() const
{
    return pointer_ctrl ? pointer_ctrl->supports_click_method(control::clicks::button_areas)
                        : false;
}

bool device::defaultClickMethodAreas() const
{
    return pointer_ctrl ? pointer_ctrl->default_click_method() == control::clicks::button_areas
                        : false;
}

bool device::isClickMethodAreas() const
{
    return pointer_ctrl ? pointer_ctrl->click_method() == control::clicks::button_areas : false;
}

bool device::supportsClickMethodClickfinger() const
{
    return pointer_ctrl ? pointer_ctrl->supports_click_method(control::clicks::finger_count)
                        : false;
}

bool device::defaultClickMethodClickfinger() const
{
    return pointer_ctrl ? pointer_ctrl->default_click_method() == control::clicks::finger_count
                        : false;
}

bool device::isClickMethodClickfinger() const
{
    return pointer_ctrl ? pointer_ctrl->click_method() == control::clicks::finger_count : false;
}

void device::setClickMethodAreas(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_click_method(control::clicks::button_areas);
    } else if (pointer_ctrl->click_method() == control::clicks::button_areas) {
        pointer_ctrl->set_click_method(control::clicks::none);
    }
}

void device::setClickMethodClickfinger(bool set)
{
    if (!pointer_ctrl) {
        return;
    }
    if (set) {
        pointer_ctrl->set_click_method(control::clicks::finger_count);
    } else if (pointer_ctrl->click_method() == control::clicks::finger_count) {
        pointer_ctrl->set_click_method(control::clicks::none);
    }
}

bool device::supportsOutputArea() const
{
    return false;
}

QRectF device::defaultOutputArea() const
{
    return {0, 0, 1, 1};
}

QRectF device::outputArea() const
{
    return {};
}

void device::setOutputArea(QRectF const& /*area*/)
{
    // Implement once we support tablet tool devices.
}

bool device::isEnabled() const
{
    return dev->is_enabled();
}

void device::setEnabled(bool enabled)
{
    dev->set_enabled(enabled);
}

bool device::isEnabledByDefault() const
{
    // We don't support "global default values" [1] yet, so always return true.
    // [1] https://invent.kde.org/plasma/kwin/-/merge_requests/1806
    return true;
}

bool device::isSwitch() const
{
    return switch_ctrl;
}

bool device::isLidSwitch() const
{
    return switch_ctrl && switch_ctrl->is_lid_switch();
}

bool device::isTabletModeSwitch() const
{
    return switch_ctrl && switch_ctrl->is_tablet_mode_switch();
}

}
