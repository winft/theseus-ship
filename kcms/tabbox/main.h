/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>
SPDX-FileCopyrightText: 2023 Ismael Asensio <isma.af@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef __MAIN_H__
#define __MAIN_H__

#include <como/win/tabbox/tabbox_config.h>

#include <kcmodule.h>
#include <ksharedconfig.h>

namespace theseus_ship
{

class KWinTabBoxConfigForm;
class KWinTabboxData;
class TabBoxSettings;

class KWinTabBoxConfig : public KCModule
{
    Q_OBJECT

public:
    explicit KWinTabBoxConfig(QObject* parent, const KPluginMetaData& data);
    ~KWinTabBoxConfig() override;

public Q_SLOTS:
    void save() override;
    void load() override;
    void defaults() override;

private Q_SLOTS:
    void updateUnmanagedState();
    void configureEffectClicked();

private:
    void initLayoutLists();
    void createConnections(KWinTabBoxConfigForm* form);

private:
    KWinTabBoxConfigForm* m_primaryTabBoxUi = nullptr;
    KWinTabBoxConfigForm* m_alternativeTabBoxUi = nullptr;
    KSharedConfigPtr m_config;

    KWinTabboxData* m_data;
};

} // namespace

#endif
