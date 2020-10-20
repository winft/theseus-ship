/*
    SPDX-FileCopyrightText: 2020 Henri Chain <henri.chain@enioka.com>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_RULES_RULE_BOOK_SETTINGS_H
#define KWIN_RULES_RULE_BOOK_SETTINGS_H

#include "rule_book_settings_base.h"
#include <KSharedConfig>

namespace KWin
{
class Rules;
class RuleSettings;

class RuleBookSettings : public RuleBookSettingsBase
{
public:
    RuleBookSettings(KSharedConfig::Ptr config, QObject* parent = nullptr);
    RuleBookSettings(const QString& configname, KConfig::OpenFlags, QObject* parent = nullptr);
    RuleBookSettings(KConfig::OpenFlags, QObject* parent = nullptr);
    RuleBookSettings(QObject* parent = nullptr);
    void setRules(const QVector<Rules*>&);
    QVector<Rules*> rules();
    bool usrSave() override;
    void usrRead() override;

private:
    QVector<RuleSettings*> m_list;
};

}

#endif
