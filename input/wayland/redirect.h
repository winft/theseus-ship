/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "fake/devices.h"

#include "input/redirect.h"

#include <KConfigWatcher>
#include <unordered_map>

namespace Wrapland::Server
{
class FakeInput;
class FakeInputDevice;
class virtual_keyboard_v1;
}

namespace KWin::input
{

template<typename Redirect>
class event_filter;
template<typename Redirect>
class window_selector_filter;

namespace wayland
{

namespace fake
{
class keyboard;
class pointer;
class touch;
}

class keyboard_redirect;
class pointer_redirect;
class tablet_redirect;
class touch_redirect;

class KWIN_EXPORT redirect : public input::redirect
{
public:
    redirect(input::platform& platform, win::space& space);
    ~redirect() override;

    bool has_tablet_mode_switch();

    void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                         QByteArray const& cursorName) override;
    void startInteractivePositionSelection(std::function<void(QPoint const&)> callback) override;
    bool isSelectingWindow() const override;

    /**
     * Adds the @p filter to the list of event filters at the last relevant position.
     *
     * Install the filter at the back of the list for a X compositor, immediately before
     * the forward filter for a Wayland compositor.
     */
    void append_filter(event_filter<redirect>* filter);
    /**
     * Adds the @p filter to the list of event filters and makes it the first
     * event filter in processing.
     *
     * Note: the event filter will get events before the lock screen can get them, thus
     * this is a security relevant method.
     */
    void prependInputEventFilter(event_filter<redirect>* filter);
    void uninstallInputEventFilter(event_filter<redirect>* filter);

    input::keyboard_redirect* get_keyboard() const override;
    input::pointer_redirect* get_pointer() const override;
    input::tablet_redirect* get_tablet() const override;
    input::touch_redirect* get_touch() const override;

    std::unique_ptr<keyboard_redirect> keyboard;
    std::unique_ptr<pointer_redirect> pointer;
    std::unique_ptr<tablet_redirect> tablet;
    std::unique_ptr<touch_redirect> touch;

    std::list<event_filter<redirect>*> m_filters;

private:
    void setup_workspace();
    void setup_devices();
    void setup_filters();
    void reconfigure();

    void handle_pointer_added(input::pointer* pointer);
    void handle_keyboard_added(input::keyboard* keyboard);
    void handle_touch_added(input::touch* touch);
    void handle_switch_added(input::switch_device* switch_device);
    void handle_fake_input_device_added(Wrapland::Server::FakeInputDevice* device);
    void handle_virtual_keyboard_added(Wrapland::Server::virtual_keyboard_v1* virtual_keyboard);

    KConfigWatcher::Ptr config_watcher;
    std::unique_ptr<Wrapland::Server::FakeInput> fake_input;

    std::unordered_map<Wrapland::Server::FakeInputDevice*, fake::devices<input::platform>>
        fake_devices;
    std::unordered_map<Wrapland::Server::virtual_keyboard_v1*, std::unique_ptr<input::keyboard>>
        virtual_keyboards;

    std::list<event_filter<redirect>*>::const_iterator m_filters_install_iterator;

    window_selector_filter<redirect>* window_selector{nullptr};
};

}
}
