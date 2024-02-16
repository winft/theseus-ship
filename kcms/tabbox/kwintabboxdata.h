/*
    SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWINTABBOXDATA_H
#define KWINTABBOXDATA_H

#include <QObject>

#include <KCModuleData>

namespace theseus_ship
{

class TabBoxSettings;
class SwitchEffectSettings;
class PluginsSettings;
class ShortcutSettings;

class KWinTabboxData : public KCModuleData
{
    Q_OBJECT

public:
    explicit KWinTabboxData(QObject* parent);

    TabBoxSettings* tabBoxConfig() const;
    TabBoxSettings* tabBoxAlternativeConfig() const;
    PluginsSettings* pluginsConfig() const;
    ShortcutSettings* shortcutConfig() const;

private:
    TabBoxSettings* m_tabBoxConfig;
    TabBoxSettings* m_tabBoxAlternativeConfig;
    PluginsSettings* m_pluginsConfig;
    ShortcutSettings* m_shortcutConfig;
};

}

#endif // KWINTABBOXDATA_H
