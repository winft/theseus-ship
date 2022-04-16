/*
 * Copyright (c) 2020 Ismael Asensio <isma.af@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KWIN_RULES_MODEL_H
#define KWIN_RULES_MODEL_H

#include "ruleitem.h"
#include "rules/rules.h"
#include "rule_settings.h"
#include "win/dbus/virtual_desktop_types.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QObject>

namespace KWin
{

class RulesModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QStringList warningMessages READ warningMessages NOTIFY warningMessagesChanged)

public:
    enum RulesRole {
        NameRole = Qt::DisplayRole,
        DescriptionRole = Qt::ToolTipRole,
        IconRole = Qt::DecorationRole,
        IconNameRole = Qt::UserRole + 1,
        KeyRole,
        SectionRole,
        EnabledRole,
        SelectableRole,
        ValueRole,
        TypeRole,
        PolicyRole,
        PolicyModelRole,
        OptionsModelRole,
        SuggestedValueRole
    };
    Q_ENUM(RulesRole)

public:
    explicit RulesModel(QObject *parent = nullptr);
    ~RulesModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex & index, const QVariant & value, int role) override;

    QModelIndex indexOf(const QString &key) const;
    bool hasRule(const QString &key) const;
    RuleItem *ruleItem(const QString &key) const;

    RuleSettings *settings() const;
    void setSettings(RuleSettings *settings);

    void setSuggestedProperties(const QVariantMap &info);

    QString description() const;
    void setDescription(const QString &description);
    QStringList warningMessages() const;

    Q_INVOKABLE void detectWindowProperties(int miliseconds);

Q_SIGNALS:
    void descriptionChanged();
    void warningMessagesChanged();

    void showSuggestions();
    void showErrorMessage(const QString &message);

    void virtualDesktopsUpdated();

private:
    void populateRuleList();
    RuleItem *addRule(RuleItem *rule);
    void writeToSettings(RuleItem *rule);

    QString defaultDescription() const;
    void processSuggestion(const QString &key, const QVariant &value);

    bool wmclassWarning() const;
    bool geometryWarning() const;

    static const QHash<QString, QString> x11PropertyHash();
    void updateVirtualDesktops();

    QList<OptionsModel::Data> windowTypesModelData() const;
    QList<OptionsModel::Data> virtualDesktopsModelData() const;
    QList<OptionsModel::Data> placementModelData() const;
    QList<OptionsModel::Data> focusModelData() const;
    QList<OptionsModel::Data> colorSchemesModelData() const;

private Q_SLOTS:
    void selectX11Window();

private:
    QList<RuleItem *> m_ruleList;
    QHash<QString, RuleItem *> m_rules;
    win::dbus::virtual_desktop_data_vector m_virtualDesktops;
    RuleSettings *m_settings;
};

}

#endif
