/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/catch_macros.h"
#include "mock_gl.h"

#include <render/gl/interface/platform.h>

#include <KConfig>
#include <KConfigGroup>
#include <catch2/generators/catch_generators.hpp>

#include <epoxy/gl.h>

void KWin::cleanupGL()
{
    GLPlatform::cleanup();
}

namespace KWin::detail::test
{

namespace
{

qint64 readVersion(KConfigGroup const& group, char const* entry)
{
    auto const parts = group.readEntry(entry, QString()).split(',');
    if (parts.count() < 2) {
        return 0;
    }

    QVector<qint64> versionParts;

    for (int i = 0; i < parts.count(); ++i) {
        bool ok = false;
        const auto value = parts.at(i).toLongLong(&ok);
        if (ok) {
            versionParts << value;
        } else {
            versionParts << 0;
        }
    }

    while (versionParts.count() < 3) {
        versionParts << 0;
    }

    return kVersionNumber(versionParts.at(0), versionParts.at(1), versionParts.at(2));
}

}

TEST_CASE("opengl platform", "[render],[unit]")
{
    cleanupGL();

    auto orig_epoxy_glGetString = epoxy_glGetString;
    auto orig_epoxy_glGetStringi = epoxy_glGetStringi;
    auto orig_epoxy_glGetIntegerv = epoxy_glGetIntegerv;

    epoxy_glGetString = mock_glGetString;
    epoxy_glGetStringi = mock_glGetStringi;
    epoxy_glGetIntegerv = mock_glGetIntegerv;

    GLPlatform::create(nullptr);

    SECTION("driver to string")
    {
        struct data {
            Driver driver;
            std::string expected;
        };

        auto test_data = GENERATE(data{Driver_R100, "Radeon"},
                                  data{Driver_R200, "R200"},
                                  data{Driver_R300C, "R300C"},
                                  data{Driver_R300G, "R300G"},
                                  data{Driver_R600C, "R600C"},
                                  data{Driver_R600G, "R600G"},
                                  data{Driver_RadeonSI, "RadeonSI"},
                                  data{Driver_Nouveau, "Nouveau"},
                                  data{Driver_Intel, "Intel"},
                                  data{Driver_NVidia, "NVIDIA"},
                                  data{Driver_Catalyst, "Catalyst"},
                                  data{Driver_Swrast, "Software rasterizer"},
                                  data{Driver_Softpipe, "softpipe"},
                                  data{Driver_Llvmpipe, "LLVMpipe"},
                                  data{Driver_VirtualBox, "VirtualBox (Chromium)"},
                                  data{Driver_VMware, "VMware (SVGA3D)"},
                                  data{Driver_Qualcomm, "Qualcomm"},
                                  data{Driver_Virgl, "Virgl (virtio-gpu, Qemu/KVM guest)"},
                                  data{Driver_Panfrost, "Panfrost"},
                                  data{Driver_Lima, "Mali (Lima)"},
                                  data{Driver_VC4, "VideoCore IV"},
                                  data{Driver_V3D, "VideoCore 3D"},
                                  data{Driver_Unknown, "Unknown"});

        REQUIRE(GLPlatform::driverToString(test_data.driver).toStdString() == test_data.expected);
    }

    SECTION("chip class to string")
    {
        struct data {
            ChipClass chip_class;
            std::string expected;
        };

        auto test_data = GENERATE(data{R100, "R100"},
                                  data{R200, "R200"},
                                  data{R300, "R300"},
                                  data{R400, "R400"},
                                  data{R500, "R500"},
                                  data{R600, "R600"},
                                  data{R700, "R700"},
                                  data{Evergreen, "EVERGREEN"},
                                  data{NorthernIslands, "Northern Islands"},
                                  data{SouthernIslands, "Southern Islands"},
                                  data{SeaIslands, "Sea Islands"},
                                  data{VolcanicIslands, "Volcanic Islands"},
                                  data{ArcticIslands, "Arctic Islands"},
                                  data{Vega, "Vega"},
                                  data{UnknownRadeon, "Unknown"},
                                  data{NV10, "NV10"},
                                  data{NV20, "NV20"},
                                  data{NV30, "NV30"},
                                  data{NV40, "NV40/G70"},
                                  data{G80, "G80/G90"},
                                  data{GF100, "GF100"},
                                  data{UnknownNVidia, "Unknown"},
                                  data{I8XX, "i830/i835"},
                                  data{I915, "i915/i945"},
                                  data{I965, "i965"},
                                  data{SandyBridge, "SandyBridge"},
                                  data{IvyBridge, "IvyBridge"},
                                  data{Haswell, "Haswell"},
                                  data{UnknownIntel, "Unknown"},
                                  data{Adreno1XX, "Adreno 1xx series"},
                                  data{Adreno2XX, "Adreno 2xx series"},
                                  data{Adreno3XX, "Adreno 3xx series"},
                                  data{Adreno4XX, "Adreno 4xx series"},
                                  data{Adreno5XX, "Adreno 5xx series"},
                                  data{UnknownAdreno, "Unknown"},
                                  data{MaliT7XX, "Mali T7xx series"},
                                  data{MaliT8XX, "Mali T8xx series"},
                                  data{MaliGXX, "Mali Gxx series"},
                                  data{UnknownPanfrost, "Unknown"},
                                  data{Mali400, "Mali 400 series"},
                                  data{Mali450, "Mali 450 series"},
                                  data{Mali470, "Mali 470 series"},
                                  data{UnknownLima, "Unknown"},
                                  data{VC4_2_1, "VideoCore IV"},
                                  data{UnknownVideoCore4, "Unknown"},
                                  data{V3D_4_2, "VideoCore 3D"},
                                  data{UnknownVideoCore3D, "Unknown"},
                                  data{UnknownChipClass, "Unknown"});

        REQUIRE(GLPlatform::chipClassToString(test_data.chip_class).toStdString()
                == test_data.expected);
    }

    SECTION("prior detect")
    {
        auto gl = GLPlatform::instance();
        QVERIFY(gl);
        QCOMPARE(gl->supports(LooseBinding), false);
        QCOMPARE(gl->supports(GLSL), false);
        QCOMPARE(gl->supports(LimitedGLSL), false);
        QCOMPARE(gl->supports(TextureNPOT), false);
        QCOMPARE(gl->supports(LimitedNPOT), false);

        QCOMPARE(gl->glVersion(), 0);
        QCOMPARE(gl->glslVersion(), 0);
        QCOMPARE(gl->mesaVersion(), 0);
        QCOMPARE(gl->galliumVersion(), 0);
        QCOMPARE(gl->serverVersion(), 0);
        QCOMPARE(gl->kernelVersion(), 0);
        QCOMPARE(gl->driverVersion(), 0);

        QCOMPARE(gl->driver(), Driver_Unknown);
        QCOMPARE(gl->chipClass(), UnknownChipClass);

        QCOMPARE(gl->isMesaDriver(), false);
        QCOMPARE(gl->isGalliumDriver(), false);
        QCOMPARE(gl->isRadeon(), false);
        QCOMPARE(gl->isNvidia(), false);
        QCOMPARE(gl->isIntel(), false);
        QCOMPARE(gl->isPanfrost(), false);
        QCOMPARE(gl->isLima(), false);
        QCOMPARE(gl->isVideoCore4(), false);
        QCOMPARE(gl->isVideoCore3D(), false);

        QCOMPARE(gl->isVirtualBox(), false);
        QCOMPARE(gl->isVMware(), false);

        QCOMPARE(gl->isSoftwareEmulation(), false);
        QCOMPARE(gl->isVirtualMachine(), false);

        QCOMPARE(gl->glVersionString(), QByteArray());
        QCOMPARE(gl->glRendererString(), QByteArray());
        QCOMPARE(gl->glVendorString(), QByteArray());
        QCOMPARE(gl->glShadingLanguageVersionString(), QByteArray());

        QCOMPARE(gl->isLooseBinding(), false);
        QCOMPARE(gl->isGLES(), false);
        REQUIRE(gl->recommend_sw());
        QCOMPARE(gl->preferBufferSubData(), false);
        QCOMPARE(gl->platformInterface(), gl_interface::unknown);
    }

    QDir dir(QFINDTESTDATA("data/glplatform"));
    auto const entries = dir.entryList(QDir::NoDotAndDotDot | QDir::Files);

    for (auto const& file : entries) {
        DYNAMIC_SECTION("dectect platform " + file.toStdString())
        {
            KConfig config(dir.absoluteFilePath(file));
            auto const driverGroup = config.group("Driver");

            s_gl = new MockGL;
            s_gl->getString.vendor = driverGroup.readEntry("Vendor").toUtf8();
            s_gl->getString.renderer = driverGroup.readEntry("Renderer").toUtf8();
            s_gl->getString.version = driverGroup.readEntry("Version").toUtf8();
            s_gl->getString.shadingLanguageVersion
                = driverGroup.readEntry("ShadingLanguageVersion").toUtf8();
            s_gl->getString.extensions
                = QVector<QByteArray>{QByteArrayLiteral("GL_ARB_shader_objects"),
                                      QByteArrayLiteral("GL_ARB_fragment_shader"),
                                      QByteArrayLiteral("GL_ARB_vertex_shader"),
                                      QByteArrayLiteral("GL_ARB_texture_non_power_of_two")};
            s_gl->getString.extensionsString = QByteArray();

            auto gl = GLPlatform::instance();
            QVERIFY(gl);
            gl->detect(gl_interface::egl);
            QCOMPARE(gl->platformInterface(), gl_interface::egl);

            auto const settingsGroup = config.group("Settings");

            QCOMPARE(gl->supports(LooseBinding), settingsGroup.readEntry("LooseBinding", false));
            QCOMPARE(gl->supports(GLSL), settingsGroup.readEntry("GLSL", false));
            QCOMPARE(gl->supports(LimitedGLSL), settingsGroup.readEntry("LimitedGLSL", false));
            QCOMPARE(gl->supports(TextureNPOT), settingsGroup.readEntry("TextureNPOT", false));
            QCOMPARE(gl->supports(LimitedNPOT), settingsGroup.readEntry("LimitedNPOT", false));

            QCOMPARE(gl->glVersion(), readVersion(settingsGroup, "GLVersion"));
            QCOMPARE(gl->glslVersion(), readVersion(settingsGroup, "GLSLVersion"));
            QCOMPARE(gl->mesaVersion(), readVersion(settingsGroup, "MesaVersion"));
            QCOMPARE(gl->galliumVersion(), readVersion(settingsGroup, "GalliumVersion"));
            QCOMPARE(gl->serverVersion(), 0);

            // Detects GL version instead of driver version
            if (file != "amd-catalyst-radeonhd-7700M-3.1.13399") {
                QCOMPARE(gl->driverVersion(), readVersion(settingsGroup, "DriverVersion"));
            }

            QCOMPARE(gl->driver(), Driver(settingsGroup.readEntry("Driver", int(Driver_Unknown))));
            QCOMPARE(gl->chipClass(),
                     ChipClass(settingsGroup.readEntry("ChipClass", int(UnknownChipClass))));

            QCOMPARE(gl->isMesaDriver(), settingsGroup.readEntry("Mesa", false));
            QCOMPARE(gl->isGalliumDriver(), settingsGroup.readEntry("Gallium", false));
            QCOMPARE(gl->isRadeon(), settingsGroup.readEntry("Radeon", false));
            QCOMPARE(gl->isNvidia(), settingsGroup.readEntry("Nvidia", false));
            QCOMPARE(gl->isIntel(), settingsGroup.readEntry("Intel", false));
            QCOMPARE(gl->isVirtualBox(), settingsGroup.readEntry("VirtualBox", false));
            QCOMPARE(gl->isVMware(), settingsGroup.readEntry("VMware", false));
            QCOMPARE(gl->isAdreno(), settingsGroup.readEntry("Adreno", false));
            QCOMPARE(gl->isPanfrost(), settingsGroup.readEntry("Panfrost", false));
            QCOMPARE(gl->isLima(), settingsGroup.readEntry("Lima", false));
            QCOMPARE(gl->isVideoCore4(), settingsGroup.readEntry("VC4", false));
            QCOMPARE(gl->isVideoCore3D(), settingsGroup.readEntry("V3D", false));
            QCOMPARE(gl->isVirgl(), settingsGroup.readEntry("Virgl", false));

            QCOMPARE(gl->isSoftwareEmulation(),
                     settingsGroup.readEntry("SoftwareEmulation", false));
            QCOMPARE(gl->isVirtualMachine(), settingsGroup.readEntry("VirtualMachine", false));

            QCOMPARE(gl->glVersionString(), s_gl->getString.version);
            QCOMPARE(gl->glRendererString(), s_gl->getString.renderer);
            QCOMPARE(gl->glVendorString(), s_gl->getString.vendor);
            QCOMPARE(gl->glShadingLanguageVersionString(), s_gl->getString.shadingLanguageVersion);

            QCOMPARE(gl->isLooseBinding(), settingsGroup.readEntry("LooseBinding", false));
            QCOMPARE(gl->isGLES(), settingsGroup.readEntry("GLES", false));
            REQUIRE(gl->recommend_sw() == (settingsGroup.readEntry("Compositor", 0) != 1));
            QCOMPARE(gl->preferBufferSubData(),
                     settingsGroup.readEntry("PreferBufferSubData", false));
        }
    }

    cleanupGL();
    delete s_gl;
    s_gl = nullptr;

    epoxy_glGetString = orig_epoxy_glGetString;
    epoxy_glGetStringi = orig_epoxy_glGetStringi;
    epoxy_glGetIntegerv = orig_epoxy_glGetIntegerv;
}

}
