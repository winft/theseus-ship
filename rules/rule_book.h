/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#ifndef KCMRULES

#include "window_rules.h"

class KXMessages;

namespace KWin
{

namespace win
{
class space;
}

class Rules;
class Toplevel;

class KWIN_EXPORT RuleBook : public QObject
{
    Q_OBJECT
public:
    RuleBook(win::space& space);
    ~RuleBook() override;

    WindowRules find(Toplevel const* window, bool);
    void discardUsed(Toplevel* window, bool withdraw);
    void setUpdatesDisabled(bool disable);
    bool areUpdatesDisabled() const;
    void load();
    void edit(Toplevel* window, bool whole_app);
    void requestDiskStorage();

    KSharedConfig::Ptr config;

private Q_SLOTS:
    void temporaryRulesMessage(const QString&);
    void cleanupTemporaryRules();
    void save();

private:
    void deleteAll();
    void initWithX11();

    QTimer* m_updateTimer;
    bool m_updatesDisabled;
    QList<Rules*> m_rules;
    QScopedPointer<KXMessages> m_temporaryRulesMessages;
    win::space& space;
};

#endif
}
