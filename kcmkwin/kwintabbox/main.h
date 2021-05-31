/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2020 Cyril Rossi <cyril.rossi@enioka.com>

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
#ifndef __MAIN_H__
#define __MAIN_H__

#include "win/tabbox/tabbox_config.h"

#include <kcmodule.h>
#include <ksharedconfig.h>

namespace KWin
{
class KWinTabBoxConfigForm;
namespace win
{
class KWinTabboxData;
class TabBoxSettings;
}


class KWinTabBoxConfig : public KCModule
{
    Q_OBJECT

public:
    explicit KWinTabBoxConfig(QWidget* parent, const QVariantList& args);
    ~KWinTabBoxConfig() override;

public Q_SLOTS:
    void save() override;
    void load() override;
    void defaults() override;

private Q_SLOTS:
    void updateUnmanagedState();
    void updateDefaultIndicator();
    void configureEffectClicked();

private:
    void updateUiFromConfig(KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);
    void updateConfigFromUi(const KWinTabBoxConfigForm *form, win::TabBoxSettings *config);
    void updateUiFromDefaultConfig(KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);
    void initLayoutLists();
    void setEnabledUi(KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);
    void createConnections(KWinTabBoxConfigForm *form);
    bool updateUnmanagedIsNeedSave(const KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);
    bool updateUnmanagedIsDefault(KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);
    void updateUiDefaultIndicator(bool visible, KWinTabBoxConfigForm *form, const win::TabBoxSettings *config);

private:
    KWinTabBoxConfigForm *m_primaryTabBoxUi = nullptr;
    KWinTabBoxConfigForm *m_alternativeTabBoxUi = nullptr;
    KSharedConfigPtr m_config;

    win::KWinTabboxData *m_data;

    // Builtin effects' names
    QString m_coverSwitch;
};

} // namespace

#endif
