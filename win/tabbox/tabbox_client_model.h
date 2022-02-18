/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef CLIENTMODEL_H
#define CLIENTMODEL_H
#include "tabbox_handler.h"

#include <QModelIndex>
/**
 * @file
 * This file defines the class client_model, the model for tabbox_clients.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */

namespace KWin
{
namespace win
{

/**
 * The model for tabbox_clients used in tabbox.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */
class tabbox_client_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum {
        ClientRole = Qt::UserRole,          ///< The tabbox_client
        CaptionRole = Qt::UserRole + 1,     ///< The caption of tabbox_client
        DesktopNameRole = Qt::UserRole + 2, ///< The name of the desktop the tabbox_client is on
        IconRole = Qt::UserRole + 3,        // TODO: to be removed
        WIdRole = Qt::UserRole + 5,         ///< The window ID of tabbox_client
        MinimizedRole = Qt::UserRole + 6,   ///< tabbox_client is minimized
        CloseableRole = Qt::UserRole + 7    ///< tabbox_client can be closed
    };
    explicit tabbox_client_model(QObject* parent = nullptr);
    ~tabbox_client_model() override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    QModelIndex
    index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QString longest_caption() const;

    /**
     * @param client The tabbox_client whose index should be returned
     * @return Returns the model_index of given tabbox_client or an invalid model_index
     * if the model does not contain the given tabbox_client.
     */
    QModelIndex index(tabbox_client* client) const;

    /**
     * Generates a new list of tabbox_clients based on the current config.
     * Calling this method will reset the model. If partial_reset is true
     * the top of the list is kept as a starting point. If not the
     * current active client is used as the starting point to generate the
     * list.
     * @param desktop The desktop for which the list should be created
     * @param partial_reset Keep the currently selected client or regenerate everything
     */
    void create_client_list(int desktop, bool partial_reset = false);
    /**
     * This method is provided as a overload for current desktop
     * @see create_client_list
     */
    void create_client_list(bool partial_reset = false);
    /**
     * @return Returns the current list of tabbox_clients.
     */
    tabbox_client_list client_list() const
    {
        return m_client_list;
    }

public Q_SLOTS:
    void close(int index);
    /**
     * Activates the client at @p index and closes the tabbox.
     * @param index The row index
     */
    void activate(int index);

private:
    tabbox_client_list m_client_list;
};

} // namespace win
} // namespace KWin

#endif // CLIENTMODEL_H
