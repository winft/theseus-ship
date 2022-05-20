/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>
Copyright 2014  Hugo Pereira Da Costa <hugo.pereira@free.fr>
Copyright 2015  Mika Allan Rauhala <mika.allan.rauhala@gmail.com>

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

#ifndef KWIN_DECORATION_PALETTE_H
#define KWIN_DECORATION_PALETTE_H

#include <kwin_export.h>

#include <KColorScheme>
#include <KConfigWatcher>
#include <KDecoration2/DecorationSettings>
#include <KSharedConfig>
#include <QFileSystemWatcher>
#include <QPalette>

#include <optional>

namespace KWin
{
namespace Decoration
{

class KWIN_EXPORT DecorationPalette : public QObject
{
    Q_OBJECT
public:
    DecorationPalette(const QString& colorScheme);

    bool isValid() const;

    QColor color(KDecoration2::ColorGroup group, KDecoration2::ColorRole role) const;
    QPalette palette() const;

Q_SIGNALS:
    void changed();

private:
    void update();

    QString m_colorScheme;
    KConfigWatcher::Ptr m_watcher;

    struct LegacyPalette {
        QPalette palette;

        QColor activeTitleBarColor;
        QColor inactiveTitleBarColor;

        QColor activeFrameColor;
        QColor inactiveFrameColor;

        QColor activeForegroundColor;
        QColor inactiveForegroundColor;
        QColor warningForegroundColor;
    };

    struct ModernPalette {
        KColorScheme active;
        KColorScheme inactive;
    };

    std::optional<LegacyPalette> m_legacyPalette;
    KSharedConfig::Ptr m_colorSchemeConfig;

    ModernPalette m_palette;
};

}
}

#endif
