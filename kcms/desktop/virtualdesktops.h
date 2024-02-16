/*
    SPDX-FileCopyrightText: 2018 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIRTUALDESKTOPS_H
#define VIRTUALDESKTOPS_H

#include <KQuickManagedConfigModule>
#include <KSharedConfig>
#include <QAbstractItemModel>

class VirtualDesktopsSettings;

namespace theseus_ship
{
class VirtualDesktopsData;
class AnimationsModel;
class DesktopsModel;

class VirtualDesktops : public KQuickManagedConfigModule
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel* desktopsModel READ desktopsModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* animationsModel READ animationsModel CONSTANT)
    Q_PROPERTY(
        VirtualDesktopsSettings* virtualDesktopsSettings READ virtualDesktopsSettings CONSTANT)

public:
    explicit VirtualDesktops(QObject* parent, const KPluginMetaData& metaData);
    ~VirtualDesktops() override;

    QAbstractItemModel* desktopsModel() const;

    QAbstractItemModel* animationsModel() const;

    VirtualDesktopsSettings* virtualDesktopsSettings() const;

    bool isDefaults() const override;
    bool isSaveNeeded() const override;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

    void configureAnimation();
    void showAboutAnimation();

private:
    VirtualDesktopsData* m_data;
};

}

#endif
