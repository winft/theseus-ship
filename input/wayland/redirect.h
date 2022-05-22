/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"

#include <KConfigWatcher>
#include <unordered_map>

namespace Wrapland::Server
{
class FakeInput;
class FakeInputDevice;
class virtual_keyboard_v1;
}

namespace KWin::input::wayland
{
namespace fake
{
class keyboard;
class pointer;
class touch;
}

class platform;

class KWIN_EXPORT redirect : public input::redirect
{
    Q_OBJECT
public:
    redirect(input::platform& platform);
    ~redirect() override;

    bool has_tablet_mode_switch();

    void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                         QByteArray const& cursorName) override;
    void startInteractivePositionSelection(std::function<void(QPoint const&)> callback) override;
    bool isSelectingWindow() const override;

    void install_shortcuts() override;

Q_SIGNALS:
    void has_tablet_mode_switch_changed(bool set);

private:
    void setup_workspace();
    void setup_devices();
    void setup_filters();
    void setup_touchpad_shortcuts();
    void reconfigure();

    void handle_pointer_added(input::pointer* pointer);
    void handle_keyboard_added(input::keyboard* keyboard);
    void handle_touch_added(input::touch* touch);
    void handle_switch_added(input::switch_device* switch_device);
    void handle_fake_input_device_added(Wrapland::Server::FakeInputDevice* device);
    void handle_virtual_keyboard_added(Wrapland::Server::virtual_keyboard_v1* virtual_keyboard);

    KConfigWatcher::Ptr config_watcher;
    std::unique_ptr<Wrapland::Server::FakeInput> fake_input;

    struct fake_input_devices {
        std::unique_ptr<fake::pointer> pointer;
        std::unique_ptr<fake::keyboard> keyboard;
        std::unique_ptr<fake::touch> touch;
    };

    std::unordered_map<Wrapland::Server::FakeInputDevice*, fake_input_devices> fake_devices;
    std::unordered_map<Wrapland::Server::virtual_keyboard_v1*, std::unique_ptr<input::keyboard>>
        virtual_keyboards;

    window_selector_filter* window_selector{nullptr};
};

}
