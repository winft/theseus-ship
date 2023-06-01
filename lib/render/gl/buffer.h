/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "texture.h"

#include "base/logging.h"
#include "render/buffer.h"

#include <memory>

namespace KWin::render::gl
{

template<typename Window, typename Scene>
class buffer : public render::buffer<Window>
{
public:
    using abstract_type = render::buffer<Window>;

    buffer(Window* window, Scene& scene)
        : render::buffer<Window>{window}
        , texture{scene.createTexture()}
    {
    }

    bool bind()
    {
        auto needs_buffer_update = [this]() {
            if (!this->win_integration) {
                return false;
            }
            // TODO(romangg): Do we need to handle X11 windows differently? Always return false like
            // before?
            return !this->win_integration->damage().isEmpty();
        };

        return std::visit(overload{[&](auto&& ref_win) {
                              if (!texture->isNull()) {
                                  if (!ref_win->render_data.damage_region.isEmpty()) {
                                      this->updateBuffer();
                                  }
                                  if (needs_buffer_update()) {
                                      texture->update_from_buffer(this);
                                      // mipmaps need to be updated
                                      texture->setDirty();
                                  }
                                  ref_win->render_data.damage_region = {};
                                  return true;
                              }
                              if (!isValid()) {
                                  return false;
                              }

                              bool success = texture->load(this);

                              if (success) {
                                  ref_win->render_data.damage_region = {};
                              } else {
                                  qCDebug(KWIN_CORE) << "Failed to bind window";
                              }
                              return success;
                          }},
                          *this->window->ref_win);
    }

    bool isValid() const override
    {
        if (!texture->isNull()) {
            return true;
        }
        return render::buffer<Window>::isValid();
    }

    std::unique_ptr<gl::texture<typename Scene::backend_t>> texture;

private:
    bool needs_buffer_update()
    {
        if (!this->win_integration) {
            return false;
        }

        // TODO(romangg): Do we need to handle X11 windows differently? Always return false like
        // before?
        return !this->win_integration->damage().isEmpty();
    }
};

}
