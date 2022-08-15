/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "book.h"

#include "base/logging.h"
#include "toplevel.h"
#include "win/control.h"

#include "book_settings.h"
#include "rules_settings.h"

#include <KConfig>
#include <KXMessages>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace KWin::win::rules
{

book::book()
    : m_updateTimer(new QTimer(this))
    , m_updatesDisabled(false)
    , m_temporaryRulesMessages()
{
    initWithX11();
    connect(kwinApp(), &Application::x11ConnectionChanged, this, &book::initWithX11);
    connect(m_updateTimer, &QTimer::timeout, this, &book::save);
    m_updateTimer->setInterval(1000);
    m_updateTimer->setSingleShot(true);
}

book::~book()
{
    save();
    deleteAll();
}

void book::initWithX11()
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        m_temporaryRulesMessages.reset();
        return;
    }
    m_temporaryRulesMessages.reset(
        new KXMessages(c, kwinApp()->x11RootWindow(), "_KDE_NET_WM_TEMPORARY_RULES", nullptr));
    connect(m_temporaryRulesMessages.data(),
            &KXMessages::gotMessage,
            this,
            &book::temporaryRulesMessage);
}

void book::deleteAll()
{
    qDeleteAll(m_rules);
    m_rules.clear();
}

window book::find(Toplevel const* window, bool ignore_temporary)
{
    QVector<ruling*> ret;
    for (auto it = m_rules.begin(); it != m_rules.end();) {
        if (ignore_temporary && (*it)->isTemporary()) {
            ++it;
            continue;
        }
        if ((*it)->match(window)) {
            auto rule = *it;
            qCDebug(KWIN_CORE) << "Rule found:" << rule << ":" << window;
            if (rule->isTemporary())
                it = m_rules.erase(it);
            else
                ++it;
            ret.append(rule);
            continue;
        }
        ++it;
    }
    return rules::window(ret);
}

void book::edit(Toplevel* window, bool whole_app)
{
    save();
    QStringList args;
    args << QStringLiteral("--uuid") << window->internal_id.toString();
    if (whole_app)
        args << QStringLiteral("--whole-app");
    auto p = new QProcess(this);
    p->setArguments(args);
    p->setProcessEnvironment(kwinApp()->processStartupEnvironment());
    const QFileInfo buildDirBinary{QDir{QCoreApplication::applicationDirPath()},
                                   QStringLiteral("kwin_rules_dialog")};
    p->setProgram(buildDirBinary.exists() ? buildDirBinary.absoluteFilePath()
                                          : QStringLiteral(KWIN_RULES_DIALOG_BIN));
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            p,
            &QProcess::deleteLater);
    connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            qCDebug(KWIN_CORE) << "Failed to start" << p->program();
        }
    });
    p->start();
}

void book::load()
{
    deleteAll();

    if (!config) {
        config = KSharedConfig::openConfig(QStringLiteral(KWIN_NAME "rulesrc"), KConfig::NoGlobals);
    } else {
        config->reparseConfiguration();
    }

    book_settings book(config);
    book.load();
    m_rules = book.rules().toList();
}

void book::save()
{
    m_updateTimer->stop();

    if (!config) {
        qCWarning(KWIN_CORE) << "book::save invoked without prior invocation of book::load";
        return;
    }

    QVector<ruling*> filteredRules;
    for (const auto& rule : qAsConst(m_rules)) {
        if (!rule->isTemporary()) {
            filteredRules.append(rule);
        }
    }

    book_settings settings(config);
    settings.setRules(filteredRules);
    settings.save();
}

void book::temporaryRulesMessage(const QString& message)
{
    auto was_temporary = false;
    for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it) {
        if ((*it)->isTemporary()) {
            was_temporary = true;
        }
    }

    auto rule = new ruling(message, true);

    // highest priority first
    m_rules.prepend(rule);

    if (!was_temporary) {
        QTimer::singleShot(60000, this, &book::cleanupTemporaryRules);
    }
}

void book::cleanupTemporaryRules()
{
    auto has_temporary = false;

    for (auto it = m_rules.begin(); it != m_rules.end();) {
        if ((*it)->discardTemporary(false)) {
            // deletes (*it)
            it = m_rules.erase(it);
        } else {
            if ((*it)->isTemporary()) {
                has_temporary = true;
            }
            ++it;
        }
    }

    if (has_temporary) {
        QTimer::singleShot(60000, this, SLOT(cleanupTemporaryRules()));
    }
}

void book::discardUsed(Toplevel* window, bool withdrawn)
{
    auto updated = false;

    for (auto it = m_rules.begin(); it != m_rules.end();) {
        if (window->control->rules.contains(*it)) {
            if ((*it)->discardUsed(withdrawn)) {
                updated = true;
            }
            if ((*it)->isEmpty()) {
                window->control->remove_rule(*it);
                auto r = *it;
                it = m_rules.erase(it);
                delete r;
                continue;
            }
        }
        ++it;
    }

    if (updated) {
        requestDiskStorage();
    }
}

void book::requestDiskStorage()
{
    m_updateTimer->start();
}

void book::setUpdatesDisabled(bool disable)
{
    m_updatesDisabled = disable;
    if (!disable) {
        Q_EMIT updates_enabled();
    }
}

bool book::areUpdatesDisabled() const
{
    return m_updatesDisabled;
}

}
