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

#pragma once

#include "rules/rule_book_settings.h"
#include "rule_settings.h"

#include <QAbstractListModel>


namespace KWin
{

class RuleBookModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum {
        DescriptionRole = Qt::DisplayRole,
    };

    explicit RuleBookModel(QObject *parent = nullptr);
    ~RuleBookModel();

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override;

    QString descriptionAt(int row) const;
    void setDescriptionAt(int row, const QString &description);

    win::rules::settings *ruleSettingsAt(int row) const;
    void setRuleSettingsAt(int row, win::rules::settings const& settings);

    void load();
    void save();
    bool isSaveNeeded();

    // Helper function to copy RuleSettings properties
    static void copySettingsTo(win::rules::settings* dest, win::rules::settings const& source);

private:
    win::rules::book_settings *m_ruleBook;
};

} // namespace
