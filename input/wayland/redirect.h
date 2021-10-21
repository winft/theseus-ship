/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"

#include <KConfigWatcher>

namespace Wrapland::Server
{
class FakeInput;
}

namespace KWin::input::wayland
{
class platform;

class KWIN_EXPORT redirect : public input::redirect
{
    Q_OBJECT
public:
    redirect();
    ~redirect();

    void set_platform(wayland::platform* platform);

    void startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                         QByteArray const& cursorName) override;
    void startInteractivePositionSelection(std::function<void(QPoint const&)> callback) override;
    bool isSelectingWindow() const override;

protected:
    void setupWorkspace() override;
    void setupInputFilters() override;

private:
    void setup_touchpad_shortcuts();
    void reconfigure();

    KConfigWatcher::Ptr config_watcher;
    std::unique_ptr<Wrapland::Server::FakeInput> fake_input;

    window_selector_filter* window_selector{nullptr};
};

}
