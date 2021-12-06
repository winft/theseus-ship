/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <vector>

namespace KWin::base
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

    // Makes a copy of all outputs. Only for external use. Prefer subclass objects instead.
    virtual std::vector<output*> get_outputs() const = 0;

Q_SIGNALS:
    void output_added(output*);
    void output_removed(output*);
};

}
