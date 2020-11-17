/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"
#include <kwin_export.h>

#include <memory>
#include <vector>

namespace KWin::win::wayland
{

class KWIN_EXPORT window : public Toplevel
{
    Q_OBJECT
public:
    bool initialized{false};
    bool mapped{false};

    void handle_commit();

    static void delete_self(window* win);

    window(Wrapland::Server::Surface* surface);
    ~window() = default;

    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    QByteArray windowRole() const override;
    double opacity() const override;

    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;

    void map();
    void unmap();

    bool isTransient() const override;

    // When another window is created, checks if this window is a subsurface for it.
    void checkTransient(Toplevel* window) override;

    void destroy() override;

    void debug(QDebug& stream) const override;
};

}
