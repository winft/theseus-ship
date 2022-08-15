/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

class KXMessages;

namespace KWin::win::rules
{

class ruling;

class KWIN_EXPORT book_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void updates_enabled();
};

class KWIN_EXPORT book
{
public:
    book();
    ~book();

    void setUpdatesDisabled(bool disable);
    bool areUpdatesDisabled() const;

    void load();
    void save();

    void requestDiskStorage();
    void temporaryRulesMessage(const QString&);

    std::unique_ptr<book_qobject> qobject;
    KSharedConfig::Ptr config;
    QList<ruling*> m_rules;

private:
    void initWithX11();
    void deleteAll();
    void cleanupTemporaryRules();

    QTimer* m_updateTimer;
    bool m_updatesDisabled;
    QScopedPointer<KXMessages> m_temporaryRulesMessages;
};

}
