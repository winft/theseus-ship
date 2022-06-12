/*
 * Copyright (c) 2011 Tamas Krutki <ktamasw@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef MODULE_H
#define MODULE_H

#include <KCModule>
#include <KPluginMetaData>
#include <KPluginModel>
#include <KQuickAddons/ConfigModule>
#include <kpluginmetadata.h>

class KJob;
class KWinScriptsData;

class Module : public KQuickAddons::ConfigModule
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QList<KPluginMetaData> pendingDeletions READ pendingDeletions NOTIFY pendingDeletionsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messageChanged)
    Q_PROPERTY(QString infoMessage READ infoMessage NOTIFY messageChanged)
public:
    explicit Module(QObject *parent, const KPluginMetaData &data, const QVariantList &args);

    void load() override;
    void save() override;
    void defaults() override;

    QAbstractItemModel *model() const
    {
        return m_model;
    }

    Q_INVOKABLE void togglePendingDeletion(const KPluginMetaData &data);
    Q_INVOKABLE bool canDeleteEntry(const KPluginMetaData &data)
    {
        return QFileInfo(data.metaDataFileName()).isWritable();
    }

    QList<KPluginMetaData> pendingDeletions()
    {
        return m_pendingDeletions;
    }

    QString errorMessage() const
    {
        return m_errorMessage;
    }
    QString infoMessage() const
    {
        return m_infoMessage;
    }
    void setErrorMessage(const QString &message)
    {
        m_infoMessage.clear();
        m_errorMessage = message;
        Q_EMIT messageChanged();
    }

    /**
     * Called when the import script button is clicked.
     */
    Q_INVOKABLE void importScript();
    Q_INVOKABLE void onGHNSEntriesChanged();

    Q_INVOKABLE void configure(const KPluginMetaData &data);

Q_SIGNALS:
    void messageChanged();
    void pendingDeletionsChanged();

private:
    void importScriptInstallFinished(KJob *job);

    KWinScriptsData *m_kwinScriptsData;
    QList<KPluginMetaData> m_pendingDeletions;
    KPluginModel *m_model;
    QString m_errorMessage;
    QString m_infoMessage;
};

#endif // MODULE_H
