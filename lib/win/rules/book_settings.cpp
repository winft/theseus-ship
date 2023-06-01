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

void book_settings::setRules(std::vector<ruling*> const& rules)
{
    mCount = rules.size();
    mRuleGroupList.clear();
    mRuleGroupList.reserve(rules.size());

    size_t i{0};
    auto const list_length = m_list.size();
    for (const auto& rule : rules) {
        rules::settings* settings;
        if (i < list_length) {
            // Optimization. Reuse settings already created
            settings = m_list.at(i);
            settings->setDefaults();
        } else {
            // If there are more rules than in cache
            settings = new rules::settings(this->sharedConfig(), QString::number(i + 1), this);
            m_list.push_back(settings);
        }

        rule->write(settings);
        mRuleGroupList.append(settings->currentGroup());

        i++;
    }

    if (m_list.empty()) {
        return;
    }

    for (size_t j = m_list.size() - 1; j >= rules.size(); j--) {
        delete m_list[j];
        m_list.erase(m_list.begin() + j);
    }
}

std::deque<ruling*> book_settings::rules()
{
    std::deque<ruling*> result;
    for (auto const& settings : m_list) {
        result.push_back(new ruling(settings));
    }
    return result;
}

bool book_settings::usrSave()
{
    bool result = true;
    for (const auto& settings : m_list) {
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

    for (const QString& groupName : qAsConst(mRuleGroupList)) {
        m_list.push_back(new settings(sharedConfig(), groupName, this));
    }
}

bool book_settings::usrIsSaveNeeded() const
{
    return isSaveNeeded() || std::any_of(m_list.cbegin(), m_list.cend(), [](const auto& settings) {
               return settings->isSaveNeeded();
           });
}

size_t book_settings::ruleCount() const
{
    return m_list.size();
}

settings* book_settings::ruleSettingsAt(size_t row) const
{
    assert(row < m_list.size());
    return m_list.at(row);
}

settings* book_settings::insertRuleSettingsAt(size_t row)
{
    assert(row < m_list.size() + 1);

    const QString groupName = generateGroupName();
    auto settings = new rules::settings(sharedConfig(), groupName, this);
    settings->setDefaults();

    m_list.insert(m_list.begin() + row, settings);
    mRuleGroupList.insert(row, groupName);
    mCount++;

    return settings;
}

void book_settings::removeRuleSettingsAt(size_t row)
{
    assert(row < m_list.size());

    delete m_list.at(row);
    m_list.erase(m_list.begin() + row);
    mRuleGroupList.removeAt(row);
    mCount--;
}

void book_settings::moveRuleSettings(size_t srcRow, size_t destRow)
{
    assert(srcRow < m_list.size() && destRow < m_list.size());

    auto el = m_list.at(srcRow);
    m_list.erase(m_list.begin() + srcRow);

    m_list.insert(m_list.begin() + destRow, el);
    mRuleGroupList.insert(destRow, mRuleGroupList.takeAt(srcRow));
}

QString book_settings::generateGroupName()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}
