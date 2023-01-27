/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/logging.h"

#include <QFileInfo>
#include <QProcess>

namespace KWin::win::rules
{

template<typename Book, typename RefWin>
void discard_used_rules(Book& book, RefWin& ref_win, bool withdrawn)
{
    auto updated = false;

    for (auto it = book.m_rules.begin(); it != book.m_rules.end();) {
        if (ref_win.control->rules.contains(*it)) {
            if ((*it)->discardUsed(withdrawn)) {
                updated = true;
            }
            if ((*it)->isEmpty()) {
                ref_win.control->remove_rule(*it);
                auto r = *it;
                it = book.m_rules.erase(it);
                delete r;
                continue;
            }
        }
        ++it;
    }

    if (updated) {
        book.requestDiskStorage();
    }
}

template<typename Book, typename RefWin>
void edit_book(Book& book, RefWin& ref_win, bool whole_app)
{
    book.save();

    QStringList args;
    args << QStringLiteral("--uuid") << ref_win.meta.internal_id.toString();

    if (whole_app) {
        args << QStringLiteral("--whole-app");
    }

    auto p = new QProcess(book.qobject.get());
    p->setArguments(args);

    if constexpr (requires(decltype(ref_win.space.base) base) { base.process_environment; }) {
        p->setProcessEnvironment(ref_win.space.base.process_environment);
    }

    QFileInfo const buildDirBinary{QDir{QCoreApplication::applicationDirPath()},
                                   QStringLiteral("kwin_rules_dialog")};
    p->setProgram(buildDirBinary.exists() ? buildDirBinary.absoluteFilePath()
                                          : QStringLiteral(KWIN_RULES_DIALOG_BIN));
    p->setProcessChannelMode(QProcess::MergedChannels);

    QObject::connect(
        p,
        static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        p,
        &QProcess::deleteLater);
    QObject::connect(
        p, &QProcess::errorOccurred, book.qobject.get(), [p](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                qCDebug(KWIN_CORE) << "Failed to start" << p->program();
            }
        });

    p->start();
}

}
