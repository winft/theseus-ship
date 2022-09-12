/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "ge_event_mem_mover.h"
#include "xinput_helpers.h"

#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "input/xkb/helpers.h"

#include <QObject>
#include <QScopedPointer>
#include <X11/extensions/XI2proto.h>
#include <X11/extensions/XInput2.h>
#include <linux/input.h>
#include <memory>

typedef struct _XDisplay Display;

namespace KWin::input::x11
{

static inline qreal fixed1616ToReal(FP1616 val)
{
    return (val)*1.0 / (1 << 16);
}

template<typename Xinput>
class XInputEventFilter : public base::x11::event_filter
{
public:
    XInputEventFilter(int xi_opcode, Xinput& xinput)
        : base::x11::event_filter(XCB_GE_GENERIC,
                                  xi_opcode,
                                  QVector<int>{XI_RawMotion,
                                               XI_RawButtonPress,
                                               XI_RawButtonRelease,
                                               XI_RawKeyPress,
                                               XI_RawKeyRelease,
                                               XI_TouchBegin,
                                               XI_TouchUpdate,
                                               XI_TouchOwnership,
                                               XI_TouchEnd})
        , xinput{xinput}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        auto pointer_device = xinput.fake_devices.pointer.get();
        auto keyboard_device = xinput.fake_devices.keyboard.get();

        GeEventMemMover ge(event);
        switch (ge->event_type) {
        case XI_RawKeyPress: {
            auto re = reinterpret_cast<xXIRawEvent*>(event);
            keyboard_key_pressed(re->detail - 8, re->time, keyboard_device);
            break;
        }
        case XI_RawKeyRelease: {
            auto re = reinterpret_cast<xXIRawEvent*>(event);
            keyboard_key_released(re->detail - 8, re->time, keyboard_device);
            break;
        }
        case XI_RawButtonPress: {
            auto e = reinterpret_cast<xXIRawEvent*>(event);
            switch (e->detail) {
            // TODO: this currently ignores left handed settings, for current usage not needed
            // if we want to use also for global mouse shortcuts, this needs to reflect state
            // correctly
            case XCB_BUTTON_INDEX_1:
                pointer_button_pressed(BTN_LEFT, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_2:
                pointer_button_pressed(BTN_MIDDLE, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_3:
                pointer_button_pressed(BTN_RIGHT, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_4:
            case XCB_BUTTON_INDEX_5:
                // vertical axis, ignore on press
                break;
                // TODO: further buttons, horizontal scrolling?
            }
        }
            if (xinput.redirect.cursor) {
                xinput.redirect.cursor->schedule_poll();
            }
            break;
        case XI_RawButtonRelease: {
            auto e = reinterpret_cast<xXIRawEvent*>(event);
            switch (e->detail) {
            // TODO: this currently ignores left handed settings, for current usage not needed
            // if we want to use also for global mouse shortcuts, this needs to reflect state
            // correctly
            case XCB_BUTTON_INDEX_1:
                pointer_button_released(BTN_LEFT, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_2:
                pointer_button_released(BTN_MIDDLE, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_3:
                pointer_button_released(BTN_RIGHT, e->time, pointer_device);
                break;
            case XCB_BUTTON_INDEX_4:
                pointer_axis_vertical(120, e->time, 0, pointer_device);
                break;
            case XCB_BUTTON_INDEX_5:
                pointer_axis_vertical(-120, e->time, 0, pointer_device);
                break;
                // TODO: further buttons, horizontal scrolling?
            }
        }
            if (xinput.redirect.cursor) {
                xinput.redirect.cursor->schedule_poll();
            }
            break;
        case XI_TouchBegin: {
            auto e = reinterpret_cast<xXIDeviceEvent*>(event);
            m_lastTouchPositions.insert(
                e->detail, QPointF(fixed1616ToReal(e->event_x), fixed1616ToReal(e->event_y)));
            break;
        }
        case XI_TouchUpdate: {
            auto e = reinterpret_cast<xXIDeviceEvent*>(event);
            const QPointF touchPosition
                = QPointF(fixed1616ToReal(e->event_x), fixed1616ToReal(e->event_y));
            if (e->detail == m_trackingTouchId) {
                const auto last = m_lastTouchPositions.value(e->detail);
                xinput.redirect.space.edges->gesture_recognizer->updateSwipeGesture(
                    QSizeF(touchPosition.x() - last.x(), touchPosition.y() - last.y()));
            }
            m_lastTouchPositions.insert(e->detail, touchPosition);
            break;
        }
        case XI_TouchEnd: {
            auto e = reinterpret_cast<xXIDeviceEvent*>(event);
            if (e->detail == m_trackingTouchId) {
                xinput.redirect.space.edges->gesture_recognizer->endSwipeGesture();
            }
            m_lastTouchPositions.remove(e->detail);
            m_trackingTouchId = 0;
            break;
        }
        case XI_TouchOwnership: {
            auto e = reinterpret_cast<xXITouchOwnershipEvent*>(event);
            auto it = m_lastTouchPositions.constFind(e->touchid);
            if (it == m_lastTouchPositions.constEnd()) {
                XIAllowTouchEvents(
                    xinput.display, e->deviceid, e->sourceid, e->touchid, XIRejectTouch);
            } else {
                if (xinput.redirect.space.edges->gesture_recognizer->startSwipeGesture(it.value())
                    > 0) {
                    m_trackingTouchId = e->touchid;
                }
                XIAllowTouchEvents(xinput.display,
                                   e->deviceid,
                                   e->sourceid,
                                   e->touchid,
                                   m_trackingTouchId == e->touchid ? XIAcceptTouch : XIRejectTouch);
            }
            break;
        }
        default:
            if (xinput.redirect.cursor) {
                xinput.redirect.cursor->schedule_poll();
            }
            break;
        }
        return false;
    }

private:
    uint32_t m_trackingTouchId = 0;
    QHash<uint32_t, QPointF> m_lastTouchPositions;

    Xinput& xinput;
};

template<typename Xinput>
class XKeyPressReleaseEventFilter : public base::x11::event_filter
{
public:
    XKeyPressReleaseEventFilter(uint32_t type, Xinput& xinput)
        : base::x11::event_filter(type)
        , xinput{xinput}
    {
    }
    ~XKeyPressReleaseEventFilter() override = default;

    bool event(xcb_generic_event_t* event) override
    {
        xcb_key_press_event_t* ke = reinterpret_cast<xcb_key_press_event_t*>(event);
        if (ke->event == ke->root) {
            const uint8_t eventType = event->response_type & ~0x80;
            auto keyboard_device = xinput.fake_devices.keyboard.get();
            if (eventType == XCB_KEY_PRESS) {
                keyboard_key_pressed(ke->detail - 8, ke->time, keyboard_device);
            } else {
                keyboard_key_released(ke->detail - 8, ke->time, keyboard_device);
            }
        }
        return false;
    }

    Xinput& xinput;
};

template<typename Platform>
struct xinput_devices {
    xinput_devices(Platform& platform)
        : keyboard{std::make_unique<input::keyboard>(platform.xkb.context,
                                                     platform.xkb.compose_table)}
        , pointer{std::make_unique<input::pointer>()}
        , platform{platform}
    {
        platform_add_keyboard(keyboard.get(), platform);
        platform_add_pointer(pointer.get(), platform);
    }

    ~xinput_devices()
    {
        platform_remove_pointer(pointer.get(), platform);
        platform_remove_keyboard(keyboard.get(), platform);
    }

    std::unique_ptr<input::keyboard> keyboard;
    std::unique_ptr<input::pointer> pointer;
    Platform& platform;
};

template<typename Redirect>
class xinput_integration : public QObject
{
public:
    using type = xinput_integration<Redirect>;

    explicit xinput_integration(Display* display, Redirect& redirect)
        : fake_devices{redirect.platform}
        , redirect{redirect}
        , display{display}
    {
        int xi_opcode, event, error;
        // init XInput extension
        if (!XQueryExtension(display, "XInputExtension", &xi_opcode, &event, &error)) {
            qCDebug(KWIN_CORE) << "XInputExtension not present";
            return;
        }

        // verify that the XInput extension is at at least version 2.0
        int major = 2, minor = 2;
        int result = XIQueryVersion(display, &major, &minor);
        if (result != Success) {
            qCDebug(KWIN_CORE) << "Failed to init XInput 2.2, trying 2.0";
            minor = 0;
            if (XIQueryVersion(display, &major, &minor) != Success) {
                qCDebug(KWIN_CORE) << "Failed to init XInput";
                return;
            }
        }
        m_hasXInput = true;
        m_xiOpcode = xi_opcode;
        m_majorVersion = major;
        m_minorVersion = minor;
        qCDebug(KWIN_CORE) << "Has XInput support" << m_majorVersion << "." << m_minorVersion;
    }

    void startListening()
    {
        // this assumes KWin is the only one setting events on the root window
        // given Qt's source code this seems to be true. If it breaks, we need to change
        XIEventMask evmasks[1];
        unsigned char mask1[XIMaskLen(XI_LASTEVENT)];

        memset(mask1, 0, sizeof(mask1));

        XISetMask(mask1, XI_RawMotion);
        XISetMask(mask1, XI_RawButtonPress);
        XISetMask(mask1, XI_RawButtonRelease);
        if (m_majorVersion >= 2 && m_minorVersion >= 1) {
            // we need to listen to all events, which is only available with XInput 2.1
            XISetMask(mask1, XI_RawKeyPress);
            XISetMask(mask1, XI_RawKeyRelease);
        }
        if (m_majorVersion >= 2 && m_minorVersion >= 2) {
            // touch events since 2.2
            XISetMask(mask1, XI_TouchBegin);
            XISetMask(mask1, XI_TouchUpdate);
            XISetMask(mask1, XI_TouchOwnership);
            XISetMask(mask1, XI_TouchEnd);
        }

        evmasks[0].deviceid = XIAllMasterDevices;
        evmasks[0].mask_len = sizeof(mask1);
        evmasks[0].mask = mask1;
        XISelectEvents(display, rootWindow(), evmasks, 1);

        setup_fake_devices();

        m_xiEventFilter.reset(new XInputEventFilter(m_xiOpcode, *this));
        m_keyPressFilter.reset(new XKeyPressReleaseEventFilter(XCB_KEY_PRESS, *this));
        m_keyReleaseFilter.reset(new XKeyPressReleaseEventFilter(XCB_KEY_RELEASE, *this));

        // install the input event spies also relevant for X11 platform
        redirect.m_spies.push_back(new input::modifier_only_shortcuts_spy(redirect));
    }

    bool hasXinput() const
    {
        return m_hasXInput;
    }

    xinput_devices<typename Redirect::platform_t> fake_devices;
    Redirect& redirect;
    Display* display;

private:
    void setup_fake_devices()
    {
        auto pointer = fake_devices.pointer.get();
        auto pointer_red = redirect.pointer.get();

        auto keyboard = fake_devices.keyboard.get();
        auto keyboard_red = redirect.keyboard.get();

        xkb::keyboard_update_from_default(redirect.platform.xkb, *keyboard->xkb);

        QObject::connect(pointer,
                         &pointer::button_changed,
                         pointer_red->qobject.get(),
                         [pointer_red](auto const& event) { pointer_red->process_button(event); });

        QObject::connect(keyboard,
                         &keyboard::key_changed,
                         keyboard_red->qobject.get(),
                         [keyboard_red](auto const& event) { keyboard_red->process_key(event); });
    }

    bool m_hasXInput = false;
    int m_xiOpcode = 0;
    int m_majorVersion = 0;
    int m_minorVersion = 0;

    QScopedPointer<XInputEventFilter<type>> m_xiEventFilter;
    QScopedPointer<XKeyPressReleaseEventFilter<type>> m_keyPressFilter;
    QScopedPointer<XKeyPressReleaseEventFilter<type>> m_keyReleaseFilter;
};

}
