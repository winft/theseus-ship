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

class Toplevel;

namespace win
{

class space;

namespace rules
{

class ruling;

class KWIN_EXPORT book : public QObject
{
    Q_OBJECT
public:
    book();
    ~book() override;

    window find(Toplevel const* window, bool);
    void discardUsed(Toplevel* window, bool withdraw);
    void setUpdatesDisabled(bool disable);
    bool areUpdatesDisabled() const;
    void load();
    void edit(Toplevel* window, bool whole_app);
    void requestDiskStorage();

    KSharedConfig::Ptr config;

Q_SIGNALS:
    void updates_enabled();

private Q_SLOTS:
    void temporaryRulesMessage(const QString&);
    void cleanupTemporaryRules();
    void save();

private:
    void deleteAll();
    void initWithX11();

    QTimer* m_updateTimer;
    bool m_updatesDisabled;
    QList<ruling*> m_rules;
    QScopedPointer<KXMessages> m_temporaryRulesMessages;
};

#endif
}
}
}
