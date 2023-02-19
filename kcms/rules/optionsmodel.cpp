/*
    SPDX-FileCopyrightText: 2020 Ismael Asensio <isma.af@gmail.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/

#include "optionsmodel.h"

#include "utils/algorithm.h"

#include <KLocalizedString>


namespace KWin
{

QHash<int, QByteArray> OptionsModel::roleNames() const
{
    return {
        {Qt::DisplayRole,    QByteArrayLiteral("display")},
        {Qt::DecorationRole, QByteArrayLiteral("decoration")},
        {Qt::ToolTipRole,    QByteArrayLiteral("tooltip")},
        {ValueRole,          QByteArrayLiteral("value")},
        {IconNameRole,       QByteArrayLiteral("iconName")},
        {OptionTypeRole,     QByteArrayLiteral("optionType")},
        {BitMaskRole,        QByteArrayLiteral("bitMask")},

    };
}

int OptionsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_data.size();
}

QVariant OptionsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return QVariant();
    }

    const Data item = m_data.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        return item.text;
    case Qt::UserRole:
        return item.value;
    case Qt::DecorationRole:
        return item.icon;
    case IconNameRole:
        return item.icon.name();
    case Qt::ToolTipRole:
        return item.description;
    case OptionTypeRole:
        return item.optionType;
    case BitMaskRole:
        return bitMask(index.row());
    }
    return QVariant();
}

int OptionsModel::selectedIndex() const
{
    return m_index;
}

int OptionsModel::indexOf(QVariant value) const
{
    for (int index = 0; index < m_data.count(); index++) {
        if (m_data.at(index).value == value) {
            return index;
        }
    }
    return -1;
}

QString OptionsModel::textOfValue(QVariant value) const
{
    int index = indexOf(value);
    if (index < 0 || index >= m_data.count()) {
        return QString();
    }
    return m_data.at(index).text;
}

QVariant OptionsModel::value() const
{
    if (m_data.isEmpty()) {
        return QVariant();
    }
    if (m_data.at(m_index).optionType == SelectAllOption) {
        return allValues();
    }
    return m_data.at(m_index).value;
}

void OptionsModel::setValue(QVariant value)
{
    if (this->value() == value) {
        return;
    }
    int index = indexOf(value);
    if (index >= 0 && index != m_index) {
        m_index = index;
        Q_EMIT selectedIndexChanged(index);
    }
}

void OptionsModel::resetValue()
{
    m_index = 0;
    Q_EMIT selectedIndexChanged(m_index);
}

bool OptionsModel::useFlags() const
{
    return m_useFlags;
}

uint OptionsModel::bitMask(int index) const
{
    const Data item = m_data.at(index);

    if (item.optionType == SelectAllOption) {
        return allOptionsMask();
    }
    if (m_useFlags) {
        return item.value.toUInt();
    }
    return 1u << index;
}

QVariant OptionsModel::allValues() const
{
    if (m_useFlags) {
        return allOptionsMask();
    }

    QVariantList list;
    for (const Data &item : qAsConst(m_data)) {
        if (item.optionType == NormalOption) {
            list << item.value;
        }
    }
    return list;
}

uint OptionsModel::allOptionsMask() const
{
    uint mask = 0;
    for (int index = 0; index < m_data.count(); index++) {
        if (m_data.at(index).optionType == NormalOption) {
            mask += bitMask(index);
        }
    }
    return mask;
}


void OptionsModel::updateModelData(const QList<Data> &data) {
    beginResetModel();
    m_data = data;
    endResetModel();
    Q_EMIT modelUpdated();
}


RulePolicy::Type RulePolicy::type() const
{
    return m_type;
}

int RulePolicy::value() const
{
    if (m_type == RulePolicy::NoPolicy) {
        // To simplify external checks when rule has no policy
        return enum_index(win::rules::action::apply);
    }
    return OptionsModel::value().toInt();
}

QString RulePolicy::policyKey(const QString &key) const
{
    switch (m_type) {
        case NoPolicy:
            return QString();
        case StringMatch:
            return QStringLiteral("%1match").arg(key);
        case SetRule:
        case ForceRule:
            return QStringLiteral("%1rule").arg(key);
    }

    return QString();
}

QList<RulePolicy::Data> RulePolicy::policyOptions(RulePolicy::Type type)
{
    static const auto stringMatchOptions = QList<RulePolicy::Data> {
        {enum_index(win::rules::name_match::unimportant), i18n("Unimportant")},
        {enum_index(win::rules::name_match::exact),       i18n("Exact Match")},
        {enum_index(win::rules::name_match::substring),   i18n("Substring Match")},
        {enum_index(win::rules::name_match::regex),      i18n("Regular Expression")}
    };

    static const auto setRuleOptions = QList<RulePolicy::Data> {
        {enum_index(win::rules::action::apply),
            i18n("Apply Initially"),
            i18n("The window property will be only set to the given value after the window is created."
                 "\nNo further changes will be affected.")},
        {enum_index(win::rules::action::apply_now),
            i18n("Apply Now"),
            i18n("The window property will be set to the given value immediately and will not be affected later"
                 "\n(this action will be deleted afterwards).")},
        {enum_index(win::rules::action::remember),
            i18n("Remember"),
            i18n("The value of the window property will be remembered and, every time the window"
                 " is created, the last remembered value will be applied.")},
        {enum_index(win::rules::action::dont_affect),
            i18n("Do Not Affect"),
            i18n("The window property will not be affected and therefore the default handling for it will be used."
                 "\nSpecifying this will block more generic window settings from taking effect.")},
        {enum_index(win::rules::action::force),
            i18n("Force"),
            i18n("The window property will be always forced to the given value.")},
        {enum_index(win::rules::action::force_temporarily),
            i18n("Force Temporarily"),
            i18n("The window property will be forced to the given value until it is hidden"
                 "\n(this action will be deleted after the window is hidden).")}
    };

    static auto forceRuleOptions = QList<RulePolicy::Data> {
        setRuleOptions.at(4),  // win::rules::action::force
        setRuleOptions.at(5),  // win::rules::action::force_temporarily
        setRuleOptions.at(3),  // win::rules::action::dont_affect
    };

    switch (type) {
    case NoPolicy:
        return {};
    case StringMatch:
        return stringMatchOptions;
    case SetRule:
        return setRuleOptions;
    case ForceRule:
        return forceRuleOptions;
    }
    return {};
}

}   //namespace
