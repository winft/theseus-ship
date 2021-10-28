/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "base/backend/wlroots.h"
#include "base/platform.h"
#include "platform.h"

#include <Wrapland/Server/drm_lease_v1.h>
#include <variant>

struct gbm_device;

namespace KWin
{

namespace base::wayland
{
class output;
}

namespace render::backend::wlroots
{

class egl_backend;
class output;

class KWIN_EXPORT backend : public Platform
{
    Q_OBJECT
public:
    base::platform<base::backend::wlroots>& base;
    egl_backend* egl{nullptr};

    QVector<output*> all_outputs;
    QVector<output*> enabled_outputs;
    int fd{0};

    explicit backend(base::platform<base::backend::wlroots>& base);
    ~backend() override;

    OpenGLBackend* createOpenGLBackend() override;
    void createEffectsHandler(render::compositor* compositor, Scene* scene) override;

    void init();

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

    void enableOutput(output* output, bool enable);

    QVector<CompositingType> supportedCompositors() const override;

    QString supportInformation() const override;

    // TODO(romangg): Used for integration tests. Make this a standalone function.
    Q_INVOKABLE void setVirtualOutputs(int count,
                                       QVector<QRect> geometries = QVector<QRect>(),
                                       QVector<int> scales = QVector<int>());

protected:
    clockid_t clockId() const override;

private:
    void init_drm_leasing();
    void process_drm_leased(Wrapland::Server::drm_lease_v1* lease);

    clockid_t m_clockId;
    base::event_receiver<backend> new_output;
};

}
}
