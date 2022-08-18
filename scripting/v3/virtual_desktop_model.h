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
class virtual_desktop;
}

namespace scripting::models::v3
{

/**
 * The virtual_desktop_model class provides a data model for the virtual desktops.
 */
class KWIN_EXPORT virtual_desktop_model : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DesktopRole = Qt::UserRole + 1,
    };

    explicit virtual_desktop_model(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

public Q_SLOTS:
    void create(uint position, const QString& name = QString());
    void remove(uint position);

private:
    win::virtual_desktop* desktopFromIndex(const QModelIndex& index) const;

    void handleVirtualDesktopAdded(win::virtual_desktop* desktop);
    void handleVirtualDesktopRemoved(win::virtual_desktop* desktop);

    QVector<win::virtual_desktop*> m_virtualDesktops;
};

}
}
