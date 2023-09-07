/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "base/platform.h"
#include <base/config-kwin.h>
#include <render/gl/interface/platform.h>

#include <KLocalizedString>
#include <QMetaProperty>
#include <QString>

namespace KWin::debug
{

// TODO(romangg): This method should be split up into the seperate modules input, render, win, etc.
template<typename Space>
QString get_support_info(Space const& space)
{
    QString support;
    const QString yes = QStringLiteral("yes\n");
    const QString no = QStringLiteral("no\n");

    support.append(ki18nc("Introductory text shown in the support information.",
                          "KWinFT Support Information:\n"
                          "The following information should be provided when openning an issue\n"
                          "ticket on https://gitlab.com/kwinft/kwinft.\n"
                          "It gives information about the currently running instance, which\n"
                          "options are used, what OpenGL driver and which effects are running.\n"
                          "Please paste the information provided underneath this introductory\n"
                          "text into a html details header and triple backticks when you\n"
                          "create an issue ticket:\n"
                          "\n<details>\n"
                          "<summary>Support Information</summary>\n"
                          "\n```\n"
                          "PASTE GOES HERE...\n"
                          "```\n"
                          "\n</details>\n")
                       .toString());

    support.append(QStringLiteral("\n==========================\n\n"));
    support.append(QStringLiteral("Version\n"));
    support.append(QStringLiteral("=======\n"));
    support.append(QStringLiteral("KWinFT version: "));
    support.append(QStringLiteral(KWIN_VERSION_STRING));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt Version: "));
    support.append(QString::fromUtf8(qVersion()));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt compile version: %1\n").arg(QStringLiteral(QT_VERSION_STR)));
    support.append(
        QStringLiteral("XCB compile version: %1\n\n").arg(QStringLiteral(XCB_VERSION_STRING)));
    support.append(QStringLiteral("Operation Mode: "));
    switch (space.base.operation_mode) {
    case base::operation_mode::x11:
        support.append(QStringLiteral("X11 only"));
        break;
    case base::operation_mode::wayland:
        support.append(QStringLiteral("Wayland Only"));
        break;
    case base::operation_mode::xwayland:
        support.append(QStringLiteral("Xwayland"));
        break;
    }
    support.append(QStringLiteral("\n\n"));

    support.append(QStringLiteral("Build Options\n"));
    support.append(QStringLiteral("=============\n"));

    support.append(QStringLiteral("KWIN_BUILD_DECORATIONS: "));
    support.append(KWIN_BUILD_DECORATIONS ? yes : no);
    support.append(QStringLiteral("KWIN_BUILD_TABBOX: "));
    support.append(KWIN_BUILD_TABBOX ? yes : no);
    support.append(QStringLiteral("KWIN_BUILD_ACTIVITIES (deprecated): "));
    support.append(no);
    support.append(QStringLiteral("HAVE_PERF: "));
    support.append(HAVE_PERF ? yes : no);
    support.append(QStringLiteral("HAVE_EPOXY_GLX: "));
    support.append(HAVE_EPOXY_GLX ? yes : no);
    support.append(QStringLiteral("\n"));

    space.debug(support);

    if (space.deco) {
        support.append(QStringLiteral("Decoration\n"));
        support.append(QStringLiteral("==========\n"));
        support.append(space.deco->supportInformation());
        support.append(QStringLiteral("\n"));
    }

    support.append(QStringLiteral("Options\n"));
    support.append(QStringLiteral("=======\n"));

    auto const metaOptions = space.base.script->options->metaObject();
    auto printProperty = [](const QVariant& variant) {
        if (variant.type() == QVariant::Size) {
            const QSize& s = variant.toSize();
            return QStringLiteral("%1x%2")
                .arg(QString::number(s.width()))
                .arg(QString::number(s.height()));
        }
        return variant.toString();
    };
    for (int i = 0; i < metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(
            QStringLiteral("%1: %2\n")
                .arg(property.name())
                .arg(printProperty(space.base.script->options->property(property.name()))));
    }

    support.append(QStringLiteral("\nScreen Edges\n"));
    support.append(QStringLiteral("============\n"));

    // TODO(romangg): The Q_PROPERTYs have been removed already for long so this won't work to get
    //                support infos on the edges. Instead add an explicit info function?
#if 0
    auto const metaScreenEdges = space.edges->metaObject();
    for (int i = 0; i < metaScreenEdges->propertyCount(); ++i) {
        const QMetaProperty property = metaScreenEdges->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(QStringLiteral("%1: %2\n")
                           .arg(property.name())
                           .arg(printProperty(space.edges->property(property.name()))));
    }
#endif
    support.append(QStringLiteral("\nScreens\n"));
    support.append(QStringLiteral("=======\n"));
    support.append(QStringLiteral("Multi-Head: "));
    support.append(QStringLiteral("not supported anymore\n"));
    support.append(QStringLiteral("Active screen follows mouse: "));
    support.append(space.options->get_current_output_follows_mouse() ? yes : no);

    auto const& outputs = space.base.outputs;
    support.append(QStringLiteral("Number of Screens: %1\n\n").arg(outputs.size()));
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto const output = outputs.at(i);
        auto const geo = output->geometry();
        support.append(QStringLiteral("Screen %1:\n").arg(i));
        support.append(QStringLiteral("---------\n"));
        support.append(QStringLiteral("Name: %1\n").arg(output->name()));
        support.append(QStringLiteral("Geometry: %1,%2,%3x%4\n")
                           .arg(geo.x())
                           .arg(geo.y())
                           .arg(geo.width())
                           .arg(geo.height()));
        support.append(QStringLiteral("Scale: %1\n").arg(output->scale()));
        support.append(QStringLiteral("Refresh Rate: %1\n\n").arg(output->refresh_rate()));
    }

    support.append(QStringLiteral("\nCompositing\n"));
    support.append(QStringLiteral("===========\n"));
    if (auto& effects = space.base.render->effects) {
        support.append(QStringLiteral("Compositing is active\n"));
        if (effects->isOpenGLCompositing()) {
            auto platform = GLPlatform::instance();
            if (platform->isGLES()) {
                support.append(QStringLiteral("Compositing Type: OpenGL ES 2.0\n"));
            } else {
                support.append(QStringLiteral("Compositing Type: OpenGL\n"));
            }
            support.append(QStringLiteral("OpenGL vendor string: ")
                           + QString::fromUtf8(platform->glVendorString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL renderer string: ")
                           + QString::fromUtf8(platform->glRendererString())
                           + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL version string: ")
                           + QString::fromUtf8(platform->glVersionString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL platform interface: "));
            switch (platform->platformInterface()) {
            case gl_interface::glx:
                support.append(QStringLiteral("GLX"));
                break;
            case gl_interface::egl:
                support.append(QStringLiteral("EGL"));
                break;
            default:
                support.append(QStringLiteral("UNKNOWN"));
            }
            support.append(QStringLiteral("\n"));

            if (platform->supports(GLSL))
                support.append(QStringLiteral("OpenGL shading language version string: ")
                               + QString::fromUtf8(platform->glShadingLanguageVersionString())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("Driver: ")
                           + GLPlatform::driverToString(platform->driver()) + QStringLiteral("\n"));
            if (!platform->isMesaDriver())
                support.append(QStringLiteral("Driver version: ")
                               + GLPlatform::versionToString(platform->driverVersion())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("GPU class: ")
                           + GLPlatform::chipClassToString(platform->chipClass())
                           + QStringLiteral("\n"));

            support.append(QStringLiteral("OpenGL version: ")
                           + GLPlatform::versionToString(platform->glVersion())
                           + QStringLiteral("\n"));

            if (platform->supports(GLSL))
                support.append(QStringLiteral("GLSL version: ")
                               + GLPlatform::versionToString(platform->glslVersion())
                               + QStringLiteral("\n"));

            if (platform->isMesaDriver())
                support.append(QStringLiteral("Mesa version: ")
                               + GLPlatform::versionToString(platform->mesaVersion())
                               + QStringLiteral("\n"));
            if (platform->kernelVersion() > 0)
                support.append(QStringLiteral("Linux kernel version: ")
                               + GLPlatform::versionToString(platform->kernelVersion())
                               + QStringLiteral("\n"));

            support.append(QStringLiteral("Direct rendering: "));
            support.append(QStringLiteral("Requires strict binding: "));
            support.append(!platform->isLooseBinding() ? yes : no);
            support.append(QStringLiteral("GLSL shaders: "));
            if (platform->supports(GLSL)) {
                support.append(yes);
            } else {
                support.append(no);
            }
            support.append(QStringLiteral("Texture NPOT support: "));
            if (platform->supports(TextureNPOT)) {
                support.append(platform->supports(LimitedNPOT) ? QStringLiteral("limited\n") : yes);
            } else {
                support.append(no);
            }
            support.append(QStringLiteral("Virtual Machine: "));
            support.append(platform->isVirtualMachine() ? yes : no);
            support.append(QStringLiteral("Timer query support: "));
            support.append(platform->supports(GLFeature::TimerQuery) ? yes : no);
            support.append(QStringLiteral("OpenGL 2 Shaders are used\n"));
        } else {
            support.append("Compositing Type: QPainter\n");
        }
        support.append(QStringLiteral("\nLoaded Effects:\n"));
        support.append(QStringLiteral("---------------\n"));
        auto const& loaded_effects = effects->loadedEffects();
        for (auto const& effect : qAsConst(loaded_effects)) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nCurrently Active Effects:\n"));
        support.append(QStringLiteral("-------------------------\n"));
        auto const& active_effects = effects->activeEffects();
        for (auto const& effect : qAsConst(active_effects)) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nEffect Settings:\n"));
        support.append(QStringLiteral("----------------\n"));
        for (auto const& effect : qAsConst(loaded_effects)) {
            support.append(effects->supportInformation(effect));
            support.append(QStringLiteral("\n"));
        }
    } else {
        support.append(QStringLiteral("Compositing is not active\n"));
    }
    return support;
}

}
