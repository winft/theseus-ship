/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/wayland/buffer.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::qpainter
{

template<typename Window>
class buffer : public render::buffer<Window>
{
public:
    using buffer_t = typename render::buffer<Window>::buffer_t;

    explicit buffer(Window* window)
        : render::buffer<Window>(window)
    {
    }

    void create() override
    {
        if (isValid()) {
            return;
        }
        render::buffer<Window>::create();
        if (!isValid()) {
            return;
        }

        // For now we rely on the fact that the QPainter backend is only run on Wayland.
        auto& win_integrate = static_cast<render::wayland::buffer_win_integration<buffer_t>&>(
            *this->win_integration);

        if (!this->window->ref_win->surface) {
            // That's an internal client.
            image = win_integrate.internal.image;
            return;
        }

        // performing deep copy, this could probably be improved
        image = win_integrate.external->shmImage()->createQImage().copy();
        if (auto s = this->window->ref_win->surface) {
            s->resetTrackedDamage();
        }
    }

    bool isValid() const override
    {
        if (!image.isNull()) {
            return true;
        }
        return render::buffer<Window>::isValid();
    }

    void updateBuffer() override
    {
        // For now we rely on the fact that the QPainter backend is only run on Wayland.
        auto& win_integrate = static_cast<render::wayland::buffer_win_integration<buffer_t>&>(
            *this->win_integration);

        auto oldBuffer = win_integrate.external.get();
        render::buffer<Window>::updateBuffer();
        auto b = win_integrate.external.get();

        if (!this->window->ref_win->surface) {
            // That's an internal client.
            image = win_integrate.internal.image;
            return;
        }
        if (!b) {
            image = QImage();
            return;
        }
        if (b == oldBuffer) {
            return;
        }

        // perform deep copy
        image = b->shmImage()->createQImage().copy();
        if (auto surface = this->window->ref_win->surface) {
            surface->resetTrackedDamage();
        }
    }

    QImage image;
};

}
