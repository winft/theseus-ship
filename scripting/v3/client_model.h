/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "scripting/window.h"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include <optional>

namespace KWin::scripting
{
class window;

namespace models::v3
{

class KWIN_EXPORT client_model : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ClientRole = Qt::UserRole + 1,
        ScreenRole,
        DesktopRole,
        ActivityRole,
    };

    explicit client_model(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

private:
    void markRoleChanged(window* client, int role);

    void handleClientAdded(window* client);
    void handleClientRemoved(window* client);
    void setupClientConnections(window* client);

    QList<window*> m_clients;
};

class KWIN_EXPORT client_filter_model : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(
        client_model* clientModel READ clientModel WRITE setClientModel NOTIFY clientModelChanged)
    Q_PROPERTY(
        QString activity READ activity WRITE setActivity RESET resetActivity NOTIFY activityChanged)
    Q_PROPERTY(KWin::win::virtual_desktop* desktop READ desktop WRITE setDesktop RESET resetDesktop
                   NOTIFY desktopChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString screenName READ screenName WRITE setScreenName RESET resetScreenName NOTIFY
                   screenNameChanged)
    Q_PROPERTY(WindowTypes windowType READ windowType WRITE setWindowType RESET resetWindowType
                   NOTIFY windowTypeChanged)

public:
    enum WindowType {
        Normal = 0x1,
        Dialog = 0x2,
        Dock = 0x4,
        Desktop = 0x8,
        Notification = 0x10,
        CriticalNotification = 0x20,
    };
    Q_DECLARE_FLAGS(WindowTypes, WindowType)
    Q_FLAG(WindowTypes)

    explicit client_filter_model(QObject* parent = nullptr);

    client_model* clientModel() const;
    void setClientModel(client_model* model);

    QString activity() const;
    void setActivity(const QString& activity);
    void resetActivity();

    win::virtual_desktop* desktop() const;
    void setDesktop(win::virtual_desktop* desktop);
    void resetDesktop();

    QString filter() const;
    void setFilter(const QString& filter);

    QString screenName() const;
    void setScreenName(const QString& screenName);
    void resetScreenName();

    WindowTypes windowType() const;
    void setWindowType(WindowTypes windowType);
    void resetWindowType();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

Q_SIGNALS:
    void activityChanged();
    void desktopChanged();
    void screenNameChanged();
    void clientModelChanged();
    void filterChanged();
    void windowTypeChanged();

private:
    WindowTypes windowTypeMask(window* client) const;

    client_model* m_clientModel = nullptr;
    base::output* m_output = nullptr;
    win::virtual_desktop* m_desktop = nullptr;
    QString m_filter;
    std::optional<WindowTypes> m_windowType;
};

}
}
