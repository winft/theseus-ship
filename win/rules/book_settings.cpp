/*
    SPDX-FileCopyrightText: 2020 Henri Chain <henri.chain@enioka.com>
    SPDX-FileCopyrightText: 2021 Ismael Asensio <isma.af@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "book_settings.h"

#include "rules_settings.h"

#include <QUuid>

namespace KWin::win::rules
{

book_settings::book_settings(KSharedConfig::Ptr config, QObject* parent)
    : book_settings_base(config, parent)
{
}

book_settings::book_settings(const QString& configname, KConfig::OpenFlags flags, QObject* parent)
    : book_settings(KSharedConfig::openConfig(configname, flags), parent)
{
}

book_settings::book_settings(KConfig::OpenFlags flags, QObject* parent)
    : book_settings(QStringLiteral("kwinrulesrc"), flags, parent)
{
}

book_settings::book_settings(QObject* parent)
    : book_settings(KConfig::FullConfig, parent)
{
}

book_settings::~book_settings()
{
    qDeleteAll(m_list);
}

void book_settings::setRules(QVector<ruling*> const& rules)
{
    mCount = rules.count();
    mRuleGroupList.clear();
    mRuleGroupList.reserve(rules.count());

    int i = 0;
    const int list_length = m_list.length();
    for (const auto& rule : rules) {
        rules::settings* settings;
        if (i < list_length) {
            // Optimization. Reuse settings already created
            settings = m_list.at(i);
            settings->setDefaults();
        } else {
            // If there are more rules than in cache
            settings = new rules::settings(this->sharedConfig(), QString::number(i + 1), this);
            m_list.append(settings);
        }

        rule->write(settings);
        mRuleGroupList.append(settings->currentGroup());

        i++;
    }

    for (int j = m_list.count() - 1; j >= rules.count(); j--) {
        delete m_list[j];
        m_list.removeAt(j);
    }
}

QVector<ruling*> book_settings::rules()
{
    QVector<ruling*> result;
    result.reserve(m_list.count());
    for (auto const& settings : qAsConst(m_list)) {
        result.append(new ruling(settings));
    }
    return result;
}

bool book_settings::usrSave()
{
    bool result = true;
    for (const auto& settings : qAsConst(m_list)) {
        result &= settings->save();
    }

    // Remove deleted groups from config
    for (const QString& groupName : qAsConst(m_storedGroups)) {
        if (sharedConfig()->hasGroup(groupName) && !mRuleGroupList.contains(groupName)) {
            sharedConfig()->deleteGroup(groupName);
        }
    }
    m_storedGroups = mRuleGroupList;

    return result;
}

void book_settings::usrRead()
{
    qDeleteAll(m_list);
    m_list.clear();

    // Legacy path for backwards compatibility with older config files without a rules list
    if (mRuleGroupList.isEmpty() && mCount > 0) {
        mRuleGroupList.reserve(mCount);
        for (int i = 1; i <= count(); i++) {
            mRuleGroupList.append(QString::number(i));
        }
        save(); // Save the generated ruleGroupList property
    }

    mCount = mRuleGroupList.count();
    m_storedGroups = mRuleGroupList;

    m_list.reserve(mRuleGroupList.count());
    for (const QString& groupName : qAsConst(mRuleGroupList)) {
        m_list.append(new settings(sharedConfig(), groupName, this));
    }
}

bool book_settings::usrIsSaveNeeded() const
{
    return isSaveNeeded() || std::any_of(m_list.cbegin(), m_list.cend(), [](const auto& settings) {
               return settings->isSaveNeeded();
           });
}

int book_settings::ruleCount() const
{
    return m_list.count();
}

settings* book_settings::ruleSettingsAt(int row) const
{
    Q_ASSERT(row >= 0 && row < m_list.count());
    return m_list.at(row);
}

settings* book_settings::insertRuleSettingsAt(int row)
{
    Q_ASSERT(row >= 0 && row < m_list.count() + 1);

    const QString groupName = generateGroupName();
    auto settings = new rules::settings(sharedConfig(), groupName, this);
    settings->setDefaults();

    m_list.insert(row, settings);
    mRuleGroupList.insert(row, groupName);
    mCount++;

    return settings;
}

void book_settings::removeRuleSettingsAt(int row)
{
    Q_ASSERT(row >= 0 && row < m_list.count());

    delete m_list.at(row);
    m_list.removeAt(row);
    mRuleGroupList.removeAt(row);
    mCount--;
}

void book_settings::moveRuleSettings(int srcRow, int destRow)
{
    Q_ASSERT(srcRow >= 0 && srcRow < m_list.count() && destRow >= 0 && destRow < m_list.count());

    m_list.insert(destRow, m_list.takeAt(srcRow));
    mRuleGroupList.insert(destRow, mRuleGroupList.takeAt(srcRow));
}

QString book_settings::generateGroupName()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}
