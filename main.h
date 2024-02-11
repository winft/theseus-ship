/*
SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KAboutData>
#include <KLocalizedString>

namespace theseus_ship
{

inline void app_create_about_data()
{
    KAboutData aboutData(QStringLiteral("theseus-ship"), // The program name used internally
                         i18n("Theseus' Ship"),          // A displayable program name string
                         QStringLiteral(""),             // The program version string
                         "",                             // Description is set per binary.
                         KAboutLicense::GPL,             // The license this code is released under
                         i18n("(c) 1999-2020, The KDE Developers"), // Copyright Statement
                         QString(),
                         QStringLiteral("winft.org"),
                         QStringLiteral("https://github.com/winft/theseus-ship/issues"));

    aboutData.addAuthor(i18n("Matthias Ettrich"), QString(), QStringLiteral("ettrich@kde.org"));
    aboutData.addAuthor(i18n("Cristian Tibirna"), QString(), QStringLiteral("tibirna@kde.org"));
    aboutData.addAuthor(i18n("Daniel M. Duley"), QString(), QStringLiteral("mosfet@kde.org"));
    aboutData.addAuthor(i18n("Luboš Luňák"), QString(), QStringLiteral("l.lunak@kde.org"));
    aboutData.addAuthor(i18n("Martin Flöser"), QString(), QStringLiteral("mgraesslin@kde.org"));
    aboutData.addAuthor(
        i18n("David Edmundson"), QString(), QStringLiteral("davidedmundson@kde.org"));
    aboutData.addAuthor(
        i18n("Vlad Zahorodnii"), QString(), QStringLiteral("vlad.zahorodnii@kde.org"));
    aboutData.addAuthor(
        i18n("Roman Gilg"), QStringLiteral("Project lead"), QStringLiteral("subdiff@gmail.com"));
    KAboutData::setApplicationData(aboutData);
}

}
