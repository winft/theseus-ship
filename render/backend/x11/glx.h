/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "glx_context_attribute_builder.h"

#include "x11_logging.h"
#include <kwineffectquickview.h>
#include <kwinglplatform.h>

#include <QOpenGLContext>
#include <QtPlatformHeaders/QGLXNativeContext>
#include <epoxy/glx.h>
#include <memory>
#include <vector>

namespace KWin::render::backend::x11
{

template<typename Backend>
GLXContext create_glx_context(Backend const& backend)
{
    GLXContext ctx{nullptr};
    auto const direct = true;

    // Use glXCreateContextAttribsARB() when it's available
    if (backend.hasExtension(QByteArrayLiteral("GLX_ARB_create_context"))) {
        auto const have_robustness
            = backend.hasExtension(QByteArrayLiteral("GLX_ARB_create_context_robustness"));
        auto const haveVideoMemoryPurge
            = backend.hasExtension(QByteArrayLiteral("GLX_NV_robustness_video_memory_purge"));

        std::vector<glx_context_attribute_builder> candidates;

        // core
        if (have_robustness) {
            if (haveVideoMemoryPurge) {
                glx_context_attribute_builder purgeMemoryCore;
                purgeMemoryCore.setVersion(3, 1);
                purgeMemoryCore.setRobust(true);
                purgeMemoryCore.setResetOnVideoMemoryPurge(true);
                candidates.emplace_back(std::move(purgeMemoryCore));
            }
            glx_context_attribute_builder robustCore;
            robustCore.setVersion(3, 1);
            robustCore.setRobust(true);
            candidates.emplace_back(std::move(robustCore));
        }
        glx_context_attribute_builder core;
        core.setVersion(3, 1);
        candidates.emplace_back(std::move(core));

        // legacy
        if (have_robustness) {
            if (haveVideoMemoryPurge) {
                glx_context_attribute_builder purgeMemoryLegacy;
                purgeMemoryLegacy.setRobust(true);
                purgeMemoryLegacy.setResetOnVideoMemoryPurge(true);
                candidates.emplace_back(std::move(purgeMemoryLegacy));
            }
            glx_context_attribute_builder robustLegacy;
            robustLegacy.setRobust(true);
            candidates.emplace_back(std::move(robustLegacy));
        }
        glx_context_attribute_builder legacy;
        legacy.setVersion(2, 1);
        candidates.emplace_back(std::move(legacy));

        for (auto& candidate : candidates) {
            auto const attribs = candidate.build();
            ctx = glXCreateContextAttribsARB(
                backend.display(), backend.fbconfig, nullptr, true, attribs.data());
            if (ctx) {
                qCDebug(KWIN_X11) << "Created GLX context with attributes:" << &candidate;
                break;
            }
        }
    }

    if (!ctx) {
        ctx = glXCreateNewContext(
            backend.display(), backend.fbconfig, GLX_RGBA_TYPE, nullptr, direct);
    }

    if (!ctx) {
        qCDebug(KWIN_X11) << "Failed to create an OpenGL context.";
        return nullptr;
    }

    if (!glXMakeCurrent(backend.display(), backend.glxWindow, ctx)) {
        qCDebug(KWIN_X11) << "Failed to make the OpenGL context current.";
        glXDestroyContext(backend.display(), ctx);
        return nullptr;
    }

    auto qtContext = new QOpenGLContext;
    QGLXNativeContext native(ctx, backend.display());
    qtContext->setNativeHandle(QVariant::fromValue(native));
    qtContext->create();
    EffectQuickView::setShareContext(std::unique_ptr<QOpenGLContext>(qtContext));

    return ctx;
}

}
