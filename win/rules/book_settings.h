/*
    SPDX-FileCopyrightText: 2020 Henri Chain <henri.chain@enioka.com>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Ismael Asensio <isma.af@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_RULES_RULE_BOOK_SETTINGS_H
#define KWIN_RULES_RULE_BOOK_SETTINGS_H

#include "rules_book_settings_base.h"
#include <KSharedConfig>

namespace KWin::win::rules
{

class ruling;
class settings;

class book_settings : public book_settings_base
{
public:
    book_settings(KSharedConfig::Ptr config, QObject* parent = nullptr);
    book_settings(const QString& configname, KConfig::OpenFlags, QObject* parent = nullptr);
    book_settings(KConfig::OpenFlags, QObject* parent = nullptr);
    book_settings(QObject* parent = nullptr);
    ~book_settings();

    void setRules(QVector<ruling*> const&);
    QVector<ruling*> rules();

    bool usrSave() override;
    void usrRead() override;
    bool usrIsSaveNeeded() const;

    int ruleCount() const;
    settings* ruleSettingsAt(int row) const;
    settings* insertRuleSettingsAt(int row);
    void removeRuleSettingsAt(int row);
    void moveRuleSettings(int srcRow, int destRow);

private:
    static QString generateGroupName();

    QVector<settings*> m_list;
    QStringList m_storedGroups;
};

}

#endif
