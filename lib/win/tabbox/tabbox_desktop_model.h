/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef DESKTOPMODEL_H
#define DESKTOPMODEL_H

#include <QModelIndex>
/**
 * @file
 * This file defines the class desktop_model, the model for desktops.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */

namespace KWin
{
namespace win
{
class tabbox_client_model;

/**
 * The model for desktops used in tabbox.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */
class tabbox_desktop_model : public QAbstractItemModel
{
public:
    enum {
        DesktopRole = Qt::UserRole,         ///< Desktop number
        DesktopNameRole = Qt::UserRole + 1, ///< Desktop name
        ClientModelRole = Qt::UserRole + 2  ///< Clients on this desktop
    };
    explicit tabbox_desktop_model(QObject* parent = nullptr);
    ~tabbox_desktop_model() override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    QModelIndex
    index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QString longest_caption() const;

    /**
     * Generates a new list of desktops based on the current config.
     * Calling this method will reset the model.
     */
    void create_desktop_list();
    /**
     * @return The current list of desktops.
     */
    QList<int> desktop_list() const
    {
        return m_desktop_list;
    }
    /**
     * @param desktop The desktop whose model_index should be retrieved
     * @return The model_index of given desktop or an invalid model_index if
     * the desktop is not in the model.
     */
    QModelIndex desktop_index(int desktop) const;

private:
    QList<int> m_desktop_list;
    QMap<int, tabbox_client_model*> m_client_models;
};

} // namespace win
} // namespace KWin
#endif // DESKTOPMODEL_H
