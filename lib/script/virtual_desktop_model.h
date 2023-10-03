/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QAbstractListModel>

namespace KWin
{

namespace win
{
class subspace;
}

namespace scripting
{

/**
 * The subspace_model class provides a data model for the virtual desktops.
 */
class KWIN_EXPORT subspace_model : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DesktopRole = Qt::UserRole + 1,
    };

    explicit subspace_model(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

public Q_SLOTS:
    win::subspace* create(uint position, const QString& name = QString());
    void remove(uint position);

private:
    win::subspace* desktopFromIndex(const QModelIndex& index) const;

    void handleVirtualDesktopAdded(win::subspace* desktop);
    void handleVirtualDesktopRemoved(win::subspace* desktop);

    QVector<win::subspace*> m_virtualDesktops;
};

}
}
