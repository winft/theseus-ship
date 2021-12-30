/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "screens.h"

#include <QObject>
#include <memory>
#include <vector>

namespace KWin
{

namespace render
{
class platform;
}

namespace base
{
class output;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    platform();
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() override;

    virtual clockid_t get_clockid() const;

    /// Makes a copy of all outputs. Only for external use. Prefer subclass objects instead.
    virtual std::vector<output*> get_outputs() const = 0;

    Screens screens;
    std::unique_ptr<render::platform> render;

Q_SIGNALS:
    void output_added(output*);
    void output_removed(output*);
};

}
}
