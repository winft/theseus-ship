/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "platform.h"
#include "platform/wlroots.h"

#include <variant>

struct gbm_device;

namespace KWin::render::backend::wlroots
{

class egl_backend;
class output;

class KWIN_EXPORT backend : public Platform
{
    Q_OBJECT
public:
    platform_base::wlroots* base;
    egl_backend* egl{nullptr};

    QVector<output*> all_outputs;
    QVector<output*> enabled_outputs;
    int fd{0};

    explicit backend(platform_base::wlroots* base, QObject* parent = nullptr);
    ~backend() override;

    OpenGLBackend* createOpenGLBackend() override;

    void init() override;
    void prepareShutdown() override;

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

    void enableOutput(output* output, bool enable);

    QVector<CompositingType> supportedCompositors() const override;

    QString supportInformation() const override;

protected:
    void doHideCursor() override;
    void doShowCursor() override;

    bool supportsClockId() const override;

private:
    clockid_t m_clockId;
    event_receiver<backend> new_output;
};

}
