/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/catch_macros.h"

#include "render/gl/context_attribute_builder.h"
#include "render/gl/egl_context_attribute_builder.h"

#include <kwinconfig.h>

#include <catch2/generators/catch_generators.hpp>
#include <epoxy/egl.h>

#if HAVE_EPOXY_GLX
#include "../render/backend/x11/glx_context_attribute_builder.h"
#include <epoxy/glx.h>

#ifndef GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV
#define GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV 0x20F7
#endif
#endif

namespace KWin::detail::test
{

class MockOpenGLContextAttributeBuilder : public render::gl::context_attribute_builder
{
public:
    std::vector<int> build() const override;
};

std::vector<int> MockOpenGLContextAttributeBuilder::build() const
{
    return std::vector<int>();
}

TEST_CASE("opengl context attribute builder", "[render],[unit]")
{
    SECTION("ctor")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isVersionRequested(), false);
        QCOMPARE(builder.majorVersion(), 0);
        QCOMPARE(builder.minorVersion(), 0);
        QCOMPARE(builder.isRobust(), false);
        QCOMPARE(builder.isForwardCompatible(), false);
        QCOMPARE(builder.isCoreProfile(), false);
        QCOMPARE(builder.isCompatibilityProfile(), false);
        QCOMPARE(builder.isResetOnVideoMemoryPurge(), false);
        QCOMPARE(builder.isHighPriority(), false);
    }

    SECTION("robust")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isRobust(), false);
        builder.setRobust(true);
        QCOMPARE(builder.isRobust(), true);
        builder.setRobust(false);
        QCOMPARE(builder.isRobust(), false);
    }

    SECTION("forward compatible")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isForwardCompatible(), false);
        builder.setForwardCompatible(true);
        QCOMPARE(builder.isForwardCompatible(), true);
        builder.setForwardCompatible(false);
        QCOMPARE(builder.isForwardCompatible(), false);
    }

    SECTION("profile")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isCoreProfile(), false);
        QCOMPARE(builder.isCompatibilityProfile(), false);
        builder.setCoreProfile(true);
        QCOMPARE(builder.isCoreProfile(), true);
        QCOMPARE(builder.isCompatibilityProfile(), false);
        builder.setCompatibilityProfile(true);
        QCOMPARE(builder.isCoreProfile(), false);
        QCOMPARE(builder.isCompatibilityProfile(), true);
        builder.setCoreProfile(true);
        QCOMPARE(builder.isCoreProfile(), true);
        QCOMPARE(builder.isCompatibilityProfile(), false);
    }

    SECTION("reset on video memory purge")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isResetOnVideoMemoryPurge(), false);
        builder.setResetOnVideoMemoryPurge(true);
        QCOMPARE(builder.isResetOnVideoMemoryPurge(), true);
        builder.setResetOnVideoMemoryPurge(false);
        QCOMPARE(builder.isResetOnVideoMemoryPurge(), false);
    }

    SECTION("version major")
    {
        MockOpenGLContextAttributeBuilder builder;
        builder.setVersion(2);
        QCOMPARE(builder.isVersionRequested(), true);
        QCOMPARE(builder.majorVersion(), 2);
        QCOMPARE(builder.minorVersion(), 0);
        builder.setVersion(3);
        QCOMPARE(builder.isVersionRequested(), true);
        QCOMPARE(builder.majorVersion(), 3);
        QCOMPARE(builder.minorVersion(), 0);
    }

    SECTION("version major and minor")
    {
        MockOpenGLContextAttributeBuilder builder;
        builder.setVersion(2, 1);
        QCOMPARE(builder.isVersionRequested(), true);
        QCOMPARE(builder.majorVersion(), 2);
        QCOMPARE(builder.minorVersion(), 1);
        builder.setVersion(3, 2);
        QCOMPARE(builder.isVersionRequested(), true);
        QCOMPARE(builder.majorVersion(), 3);
        QCOMPARE(builder.minorVersion(), 2);
    }

    SECTION("high priority")
    {
        MockOpenGLContextAttributeBuilder builder;
        QCOMPARE(builder.isHighPriority(), false);
        builder.setHighPriority(true);
        QCOMPARE(builder.isHighPriority(), true);
        builder.setHighPriority(false);
        QCOMPARE(builder.isHighPriority(), false);
    }

    SECTION("egl")
    {
        struct data {
            bool request_version;
            int major;
            int minor;
            bool robust;
            bool forward_compat;
            bool core_profile;
            bool compat_profile;
            bool high_priority;
            std::vector<int> expected_attribs;
        };

        auto test_data = GENERATE(
            // fallback
            data{false, 0, 0, false, false, false, false, false, {EGL_NONE}},
            // legacy/robust
            data{false,
                 0,
                 0,
                 true,
                 false,
                 false,
                 false,
                 false,
                 {
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
                     EGL_NONE,
                 }},
            // legacy/robust/high priority
            data{false,
                 0,
                 0,
                 true,
                 false,
                 false,
                 false,
                 true,
                 {
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core
            data{true,
                 3,
                 1,
                 false,
                 false,
                 false,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_NONE,
                 }},
            // core/high priority
            data{true,
                 3,
                 1,
                 false,
                 false,
                 false,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core/robust
            data{true,
                 3,
                 1,
                 true,
                 false,
                 false,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
                     EGL_NONE,
                 }},
            // core/robust/high priority
            data{true,
                 3,
                 1,
                 true,
                 false,
                 false,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core/robust/forward compatible
            data{true,
                 3,
                 1,
                 true,
                 true,
                 false,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_NONE,
                 }},
            // core/robust/forward compatible/high priority
            data{true,
                 3,
                 1,
                 true,
                 true,
                 false,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core/forward compatible
            data{true,
                 3,
                 1,
                 false,
                 true,
                 false,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_NONE,
                 }},
            // core/forward compatible/high priority
            data{true,
                 3,
                 1,
                 false,
                 true,
                 false,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     1,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core profile/forward compatible
            data{true,
                 3,
                 2,
                 false,
                 true,
                 true,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                     EGL_NONE,
                 }},
            // core profile/forward compatible/high priority
            data{true,
                 3,
                 2,
                 false,
                 true,
                 true,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // compatibility profile/forward compatible
            data{true,
                 3,
                 2,
                 false,
                 true,
                 false,
                 true,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
                     EGL_NONE,
                 }},
            // compatibility profile/forward compatible/high priority
            data{true,
                 3,
                 2,
                 false,
                 true,
                 false,
                 true,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // core profile/robust/forward compatible
            data{true,
                 3,
                 2,
                 true,
                 true,
                 true,
                 false,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                     EGL_NONE,
                 }},
            // core profile/robust/forward compatible/high priority
            data{true,
                 3,
                 2,
                 true,
                 true,
                 true,
                 false,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }},
            // compatibility profile/robust/forward compatible
            data{true,
                 3,
                 2,
                 true,
                 true,
                 false,
                 true,
                 false,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
                     EGL_NONE,
                 }},
            // compatibility profile/robust/forward compatible/high priority
            data{true,
                 3,
                 2,
                 true,
                 true,
                 false,
                 true,
                 true,
                 {
                     EGL_CONTEXT_MAJOR_VERSION_KHR,
                     3,
                     EGL_CONTEXT_MINOR_VERSION_KHR,
                     2,
                     EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
                     EGL_LOSE_CONTEXT_ON_RESET_KHR,
                     EGL_CONTEXT_FLAGS_KHR,
                     EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR
                         | EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                     EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                     EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
                     EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                     EGL_CONTEXT_PRIORITY_HIGH_IMG,
                     EGL_NONE,
                 }});

        render::gl::egl_context_attribute_builder builder;
        if (test_data.request_version) {
            builder.setVersion(test_data.major, test_data.minor);
        }
        builder.setRobust(test_data.robust);
        builder.setForwardCompatible(test_data.forward_compat);
        builder.setCoreProfile(test_data.core_profile);
        builder.setCompatibilityProfile(test_data.compat_profile);
        builder.setHighPriority(test_data.high_priority);

        auto attribs = builder.build();
        REQUIRE(attribs == test_data.expected_attribs);
    }

    SECTION("gles")
    {
        struct data {
            bool robust;
            bool high_priority;
            std::vector<int> expected_attribs;
        };

        auto test_data = GENERATE(data{true,
                                       false,
                                       {
                                           EGL_CONTEXT_CLIENT_VERSION,
                                           2,
                                           EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT,
                                           EGL_TRUE,
                                           EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
                                           EGL_LOSE_CONTEXT_ON_RESET_EXT,
                                           EGL_NONE,
                                       }},
                                  data{true,
                                       true,
                                       {
                                           EGL_CONTEXT_CLIENT_VERSION,
                                           2,
                                           EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT,
                                           EGL_TRUE,
                                           EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
                                           EGL_LOSE_CONTEXT_ON_RESET_EXT,
                                           EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                                           EGL_CONTEXT_PRIORITY_HIGH_IMG,
                                           EGL_NONE,
                                       }},
                                  data{false, false, {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE}},
                                  data{false,
                                       true,
                                       {
                                           EGL_CONTEXT_CLIENT_VERSION,
                                           2,
                                           EGL_CONTEXT_PRIORITY_LEVEL_IMG,
                                           EGL_CONTEXT_PRIORITY_HIGH_IMG,
                                           EGL_NONE,
                                       }});

        render::gl::egl_gles_context_attribute_builder builder;
        builder.setVersion(2);
        builder.setRobust(test_data.robust);
        builder.setHighPriority(test_data.high_priority);

        auto attribs = builder.build();
        REQUIRE(attribs == test_data.expected_attribs);
    }

#if HAVE_EPOXY_GLX
    SECTION("glx")
    {
        struct data {
            bool request_version;
            int major;
            int minor;
            bool robust;
            bool video_purge;
            std::vector<int> expected_attribs;
        };

        auto test_data = GENERATE(
            // fallback
            data{true,
                 2,
                 1,
                 false,
                 false,
                 {GLX_CONTEXT_MAJOR_VERSION_ARB, 2, GLX_CONTEXT_MINOR_VERSION_ARB, 1, 0}},
            // legacy/robust/videoPurge
            data{false,
                 0,
                 0,
                 true,
                 true,
                 {
                     GLX_CONTEXT_FLAGS_ARB,
                     GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                     GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                     GLX_LOSE_CONTEXT_ON_RESET_ARB,
                     GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV,
                     GL_TRUE,
                     0,
                 }},
            // core
            data{true,
                 3,
                 1,
                 false,
                 false,
                 {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 1, 0}},
            // core/robust
            data{true,
                 3,
                 1,
                 true,
                 false,
                 {
                     GLX_CONTEXT_MAJOR_VERSION_ARB,
                     3,
                     GLX_CONTEXT_MINOR_VERSION_ARB,
                     1,
                     GLX_CONTEXT_FLAGS_ARB,
                     GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                     GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                     GLX_LOSE_CONTEXT_ON_RESET_ARB,
                     0,
                 }},
            // core/robust/videoPurge
            data{true,
                 3,
                 1,
                 true,
                 true,
                 {
                     GLX_CONTEXT_MAJOR_VERSION_ARB,
                     3,
                     GLX_CONTEXT_MINOR_VERSION_ARB,
                     1,
                     GLX_CONTEXT_FLAGS_ARB,
                     GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                     GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB,
                     GLX_LOSE_CONTEXT_ON_RESET_ARB,
                     GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV,
                     GL_TRUE,
                     0,
                 }});

        render::backend::x11::glx_context_attribute_builder builder;
        if (test_data.request_version) {
            builder.setVersion(test_data.major, test_data.minor);
        }
        builder.setRobust(test_data.robust);
        builder.setResetOnVideoMemoryPurge(test_data.video_purge);

        auto attribs = builder.build();
        REQUIRE(attribs == test_data.expected_attribs);
    }
#endif
}

}
