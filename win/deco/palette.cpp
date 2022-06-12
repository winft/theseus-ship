/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>
Copyright 2014  Hugo Pereira Da Costa <hugo.pereira@free.fr>
Copyright 2015  Mika Allan Rauhala <mika.allan.rauhala@gmail.com>
Copyright 2020  Carson Black <uhhadd@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "palette.h"

#include "decorations_logging.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QFileInfo>
#include <QPalette>
#include <QStandardPaths>

namespace KWin::win::deco
{

palette::palette(const QString& colorScheme)
    : m_colorScheme(colorScheme != QStringLiteral("kdeglobals") ? colorScheme : QString())
{
    if (m_colorScheme.isEmpty()) {
        m_colorSchemeConfig = KSharedConfig::openConfig(m_colorScheme, KConfig::FullConfig);
    } else {
        m_colorSchemeConfig = KSharedConfig::openConfig(m_colorScheme, KConfig::SimpleConfig);
    }
    m_watcher = KConfigWatcher::create(m_colorSchemeConfig);

    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, &palette::update);

    update();
}

bool palette::isValid() const
{
    return true;
}

QColor palette::color(KDecoration2::ColorGroup group, KDecoration2::ColorRole role) const
{
    using KDecoration2::ColorGroup;
    using KDecoration2::ColorRole;

    if (m_legacyColors.has_value()) {
        switch (role) {
        case ColorRole::Frame:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeFrameColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveFrameColor;
            default:
                return QColor();
            }
        case ColorRole::TitleBar:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeTitleBarColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveTitleBarColor;
            default:
                return QColor();
            }
        case ColorRole::Foreground:
            switch (group) {
            case ColorGroup::Active:
                return m_legacyColors->activeForegroundColor;
            case ColorGroup::Inactive:
                return m_legacyColors->inactiveForegroundColor;
            case ColorGroup::Warning:
                return m_legacyColors->warningForegroundColor;
            default:
                return QColor();
            }
        default:
            return QColor();
        }
    }

    switch (role) {
    case ColorRole::Frame:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.background().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.background().color();
        default:
            return QColor();
        }
    case ColorRole::TitleBar:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.background().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.background().color();
        default:
            return QColor();
        }
    case ColorRole::Foreground:
        switch (group) {
        case ColorGroup::Active:
            return m_colors.active.foreground().color();
        case ColorGroup::Inactive:
            return m_colors.inactive.foreground().color();
        case ColorGroup::Warning:
            return m_colors.inactive.foreground(KColorScheme::ForegroundRole::NegativeText).color();
        default:
            return QColor();
        }
    default:
        return QColor();
    }
}

QPalette palette::get_qt_palette() const
{
    return m_palette;
}

void palette::update()
{
    m_colorSchemeConfig->sync();
    m_palette = KColorScheme::createApplicationPalette(m_colorSchemeConfig);

    if (KColorScheme::isColorSetSupported(m_colorSchemeConfig, KColorScheme::Header)) {
        m_colors.active = KColorScheme(QPalette::Normal, KColorScheme::Header, m_colorSchemeConfig);
        m_colors.inactive
            = KColorScheme(QPalette::Inactive, KColorScheme::Header, m_colorSchemeConfig);
        m_legacyColors.reset();
    } else {
        KConfigGroup wmConfig(m_colorSchemeConfig, QStringLiteral("WM"));

        if (!wmConfig.exists()) {
            m_colors.active
                = KColorScheme(QPalette::Normal, KColorScheme::Window, m_colorSchemeConfig);
            m_colors.inactive
                = KColorScheme(QPalette::Inactive, KColorScheme::Window, m_colorSchemeConfig);
            m_legacyColors.reset();
            return;
        }

        m_legacyColors = LegacyColors{};
        m_legacyColors->activeFrameColor
            = wmConfig.readEntry("frame", m_palette.color(QPalette::Active, QPalette::Window));
        m_legacyColors->inactiveFrameColor
            = wmConfig.readEntry("inactiveFrame", m_legacyColors->activeFrameColor);
        m_legacyColors->activeTitleBarColor = wmConfig.readEntry(
            "activeBackground", m_palette.color(QPalette::Active, QPalette::Highlight));
        m_legacyColors->inactiveTitleBarColor
            = wmConfig.readEntry("inactiveBackground", m_legacyColors->inactiveTitleBarColor);
        m_legacyColors->activeForegroundColor = wmConfig.readEntry(
            "activeForeground", m_palette.color(QPalette::Active, QPalette::HighlightedText));
        m_legacyColors->inactiveForegroundColor = wmConfig.readEntry(
            "inactiveForeground", m_legacyColors->activeForegroundColor.darker());

        KConfigGroup windowColorsConfig(m_colorSchemeConfig, QStringLiteral("Colors:Window"));
        m_legacyColors->warningForegroundColor
            = windowColorsConfig.readEntry("ForegroundNegative", QColor(237, 21, 2));
    }

    Q_EMIT changed();
}

}
