/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_context_attribute_builder.h"
#include <wayland_logging.h>

#include <QOpenGLContext>
#include <epoxy/egl.h>
#include <memory>
#include <vector>

namespace KWin::render::gl
{

static bool is_gles_render()
{
    if (qstrcmp(qgetenv("KWIN_COMPOSE"), "O2ES") == 0) {
        return true;
    }
    return QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES;
}

template<typename Backend>
EGLContext create_egl_context(Backend const& backend)
{
    auto const have_robustness
        = backend.hasExtension(QByteArrayLiteral("EGL_EXT_create_context_robustness"));
    auto const have_create_context
        = backend.hasExtension(QByteArrayLiteral("EGL_KHR_create_context"));
    const bool have_context_priority
        = backend.hasExtension(QByteArrayLiteral("EGL_IMG_context_priority"));

    std::vector<std::unique_ptr<context_attribute_builder>> candidates;

    if (is_gles_render()) {
        if (have_create_context && have_robustness && have_context_priority) {
            auto glesRobustPriority = std::make_unique<egl_gles_context_attribute_builder>();
            glesRobustPriority->setVersion(2);
            glesRobustPriority->setRobust(true);
            glesRobustPriority->setHighPriority(true);
            candidates.push_back(std::move(glesRobustPriority));
        }
        if (have_create_context && have_robustness) {
            auto glesRobust = std::make_unique<egl_gles_context_attribute_builder>();
            glesRobust->setVersion(2);
            glesRobust->setRobust(true);
            candidates.push_back(std::move(glesRobust));
        }
        if (have_context_priority) {
            auto glesPriority = std::make_unique<egl_gles_context_attribute_builder>();
            glesPriority->setVersion(2);
            glesPriority->setHighPriority(true);
            candidates.push_back(std::move(glesPriority));
        }

        auto gles = std::make_unique<egl_gles_context_attribute_builder>();
        gles->setVersion(2);
        candidates.push_back(std::move(gles));
    } else {
        if (have_create_context) {
            if (have_robustness && have_context_priority) {
                auto robustCorePriority = std::make_unique<egl_context_attribute_builder>();
                robustCorePriority->setVersion(3, 1);
                robustCorePriority->setRobust(true);
                robustCorePriority->setHighPriority(true);
                candidates.push_back(std::move(robustCorePriority));
            }
            if (have_robustness) {
                auto robustCore = std::make_unique<egl_context_attribute_builder>();
                robustCore->setVersion(3, 1);
                robustCore->setRobust(true);
                candidates.push_back(std::move(robustCore));
            }
            if (have_context_priority) {
                auto corePriority = std::make_unique<egl_context_attribute_builder>();
                corePriority->setVersion(3, 1);
                corePriority->setHighPriority(true);
                candidates.push_back(std::move(corePriority));
            }
            auto core = std::make_unique<egl_context_attribute_builder>();
            core->setVersion(3, 1);
            candidates.push_back(std::move(core));
        }
        if (have_robustness && have_create_context && have_context_priority) {
            auto robustPriority = std::make_unique<egl_context_attribute_builder>();
            robustPriority->setRobust(true);
            robustPriority->setHighPriority(true);
            candidates.push_back(std::move(robustPriority));
        }
        if (have_robustness && have_create_context) {
            auto robust = std::make_unique<egl_context_attribute_builder>();
            robust->setRobust(true);
            candidates.push_back(std::move(robust));
        }
        candidates.emplace_back(new egl_context_attribute_builder);
    }

    auto ctx = EGL_NO_CONTEXT;
    for (auto& candidate : candidates) {
        auto const attribs = candidate->build();
        ctx = eglCreateContext(
            backend.data.base.display, backend.data.base.config, EGL_NO_CONTEXT, attribs.data());
        if (ctx != EGL_NO_CONTEXT) {
            qCDebug(KWIN_WL) << "Created EGL context with attributes:" << candidate.get();
            break;
        }
    }

    if (ctx == EGL_NO_CONTEXT) {
        qCCritical(KWIN_WL) << "Create Context failed";
        return ctx;
    }

    return ctx;
}

}
