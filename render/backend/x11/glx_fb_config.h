/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "x11_logging.h"

#include <kwinxrender/utils.h>

#include <epoxy/glx.h>

namespace KWin::render::backend::x11
{

class fb_config_info
{
public:
    GLXFBConfig fbconfig;
    int bind_texture_format;
    int texture_targets;
    int y_inverted;
    int mipmap;
};

template<typename Backend>
fb_config_info* fb_config_info_for_visual(xcb_visualid_t visual, Backend& backend)
{
    auto it = backend.fb_configs.find(visual);
    if (it != backend.fb_configs.end()) {
        return it->second;
    }

    auto info = new fb_config_info;
    info->fbconfig = nullptr;
    info->bind_texture_format = 0;
    info->texture_targets = 0;
    info->y_inverted = 0;
    info->mipmap = 0;

    backend.fb_configs.insert({visual, info});

    auto const format = XRenderUtils::findPictFormat(visual);
    auto const direct = XRenderUtils::findPictFormatInfo(format);

    if (!direct) {
        qCCritical(KWIN_X11).nospace()
            << "Could not find a picture format for visual 0x" << hex << visual;
        return info;
    }

    auto bitCount = [](uint32_t mask) {
#if defined(__GNUC__)
        return __builtin_popcount(mask);
#else
        int count = 0;

        while (mask) {
            count += (mask & 1);
            mask >>= 1;
        }

        return count;
#endif
    };

    const int red_bits = bitCount(direct->red_mask);
    const int green_bits = bitCount(direct->green_mask);
    const int blue_bits = bitCount(direct->blue_mask);
    const int alpha_bits = bitCount(direct->alpha_mask);

    const int depth = backend.visualDepth(visual);

    const auto rgb_sizes = std::tie(red_bits, green_bits, blue_bits);

    const int attribs[]
        = {GLX_RENDER_TYPE,
           GLX_RGBA_BIT,
           GLX_DRAWABLE_TYPE,
           GLX_WINDOW_BIT | GLX_PIXMAP_BIT,
           GLX_X_VISUAL_TYPE,
           GLX_TRUE_COLOR,
           GLX_X_RENDERABLE,
           True,
           GLX_CONFIG_CAVEAT,
           int(GLX_DONT_CARE), // The ARGB32 visual is marked non-conformant in Catalyst
           GLX_FRAMEBUFFER_SRGB_CAPABLE_EXT,
           int(GLX_DONT_CARE), // The ARGB32 visual is marked sRGB capable in mesa/i965
           GLX_BUFFER_SIZE,
           red_bits + green_bits + blue_bits + alpha_bits,
           GLX_RED_SIZE,
           red_bits,
           GLX_GREEN_SIZE,
           green_bits,
           GLX_BLUE_SIZE,
           blue_bits,
           GLX_ALPHA_SIZE,
           alpha_bits,
           GLX_STENCIL_SIZE,
           0,
           GLX_DEPTH_SIZE,
           0,
           0};

    auto display = backend.data.display;
    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, DefaultScreen(display), attribs, &count);

    if (count < 1) {
        qCCritical(KWIN_X11).nospace()
            << "Could not find a framebuffer configuration for visual 0x" << hex << visual;
        return info;
    }

    struct FBConfig {
        GLXFBConfig config;
        int depth;
        int stencil;
        int format;
    };

    std::deque<FBConfig> candidates;

    for (int i = 0; i < count; i++) {
        int red, green, blue;
        glXGetFBConfigAttrib(display, configs[i], GLX_RED_SIZE, &red);
        glXGetFBConfigAttrib(display, configs[i], GLX_GREEN_SIZE, &green);
        glXGetFBConfigAttrib(display, configs[i], GLX_BLUE_SIZE, &blue);

        if (std::tie(red, green, blue) != rgb_sizes)
            continue;

        xcb_visualid_t visual;
        glXGetFBConfigAttrib(display, configs[i], GLX_VISUAL_ID, (int*)&visual);

        if (backend.visualDepth(visual) != depth)
            continue;

        int bind_rgb, bind_rgba;
        glXGetFBConfigAttrib(display, configs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &bind_rgba);
        glXGetFBConfigAttrib(display, configs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &bind_rgb);

        if (!bind_rgb && !bind_rgba)
            continue;

        int depth, stencil;
        glXGetFBConfigAttrib(display, configs[i], GLX_DEPTH_SIZE, &depth);
        glXGetFBConfigAttrib(display, configs[i], GLX_STENCIL_SIZE, &stencil);

        int texture_format;
        if (alpha_bits)
            texture_format = bind_rgba ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT;
        else
            texture_format = bind_rgb ? GLX_TEXTURE_FORMAT_RGB_EXT : GLX_TEXTURE_FORMAT_RGBA_EXT;

        candidates.emplace_back(FBConfig{configs[i], depth, stencil, texture_format});
    }

    if (count > 0)
        XFree(configs);

    std::stable_sort(
        candidates.begin(), candidates.end(), [](const FBConfig& left, const FBConfig& right) {
            if (left.depth < right.depth)
                return true;

            if (left.stencil < right.stencil)
                return true;

            return false;
        });

    if (candidates.size() > 0) {
        const FBConfig& candidate = candidates.front();

        int y_inverted, texture_targets;
        glXGetFBConfigAttrib(
            display, candidate.config, GLX_BIND_TO_TEXTURE_TARGETS_EXT, &texture_targets);
        glXGetFBConfigAttrib(display, candidate.config, GLX_Y_INVERTED_EXT, &y_inverted);

        info->fbconfig = candidate.config;
        info->bind_texture_format = candidate.format;
        info->texture_targets = texture_targets;
        info->y_inverted = y_inverted;
        info->mipmap = 0;
    }

    if (info->fbconfig) {
        int fbc_id = 0;
        int visual_id = 0;

        glXGetFBConfigAttrib(display, info->fbconfig, GLX_FBCONFIG_ID, &fbc_id);
        glXGetFBConfigAttrib(display, info->fbconfig, GLX_VISUAL_ID, &visual_id);

        qCDebug(KWIN_X11).nospace()
            << "Using FBConfig 0x" << hex << fbc_id << " for visual 0x" << hex << visual_id;
    }

    return info;
}

}
