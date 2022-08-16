/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "book.h"

#include "base/logging.h"
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
    : qobject{std::make_unique<book_qobject>()}
    , m_updateTimer(new QTimer(qobject.get()))
    , m_updatesDisabled(false)
    , m_temporaryRulesMessages()
{
    initWithX11();
    QObject::connect(
        kwinApp(), &Application::x11ConnectionChanged, qobject.get(), [this] { initWithX11(); });
    QObject::connect(m_updateTimer, &QTimer::timeout, qobject.get(), [this] { save(); });
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
    m_temporaryRulesMessages = std::make_unique<KXMessages>(
        c, kwinApp()->x11RootWindow(), "_KDE_NET_WM_TEMPORARY_RULES", nullptr);
    QObject::connect(m_temporaryRulesMessages.get(),
                     &KXMessages::gotMessage,
                     qobject.get(),
                     [this](auto const& message) { temporaryRulesMessage(message); });
}

void book::deleteAll()
{
    qDeleteAll(m_rules);
    m_rules.clear();
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
    m_rules = book.rules();
}

void book::save()
{
    m_updateTimer->stop();

    if (!config) {
        qCWarning(KWIN_CORE) << "book::save invoked without prior invocation of book::load";
        return;
    }

    std::vector<ruling*> filteredRules;
    for (const auto& rule : qAsConst(m_rules)) {
        if (!rule->isTemporary()) {
            filteredRules.push_back(rule);
        }
    }

    book_settings settings(config);
    settings.setRules(filteredRules);
    settings.save();
}

void book::temporaryRulesMessage(const QString& message)
{
    auto was_temporary = false;
    for (auto&& rule : m_rules) {
        if (rule->isTemporary()) {
            was_temporary = true;
        }
    }

    auto rule = new ruling(message, true);

    // highest priority first
    m_rules.push_front(rule);

    if (!was_temporary) {
        QTimer::singleShot(60000, qobject.get(), [this] { cleanupTemporaryRules(); });
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
        QTimer::singleShot(60000, qobject.get(), [this] { cleanupTemporaryRules(); });
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
        Q_EMIT qobject->updates_enabled();
    }
}

bool book::areUpdatesDisabled() const
{
    return m_updatesDisabled;
}

}
