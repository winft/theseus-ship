/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __TOUCH_H__
#define __TOUCH_H__

#include <kcmodule.h>
#include <ksharedconfig.h>

#include <como/win/types.h>

class QShowEvent;

namespace theseus_ship
{
class KWinTouchScreenData;
class KWinTouchScreenEdgeConfigForm;
class KWinTouchScreenScriptSettings;
class KWinTouchScreenEdgeEffectSettings;

class KWinScreenEdgesConfig : public KCModule
{
    Q_OBJECT

public:
    explicit KWinScreenEdgesConfig(QObject* parent, const KPluginMetaData& data);
    ~KWinScreenEdgesConfig() override;

public Q_SLOTS:
    void save() override;
    void load() override;
    void defaults() override;

private:
    KWinTouchScreenEdgeConfigForm* m_form;
    KSharedConfigPtr m_config;
    QStringList m_effects; // list of effect IDs ordered in the list they are presented in the menu
    QStringList m_scripts; // list of script IDs ordered in the list they are presented in the menu
    QHash<QString, KWinTouchScreenScriptSettings*> m_scriptSettings;
    QHash<QString, KWinTouchScreenEdgeEffectSettings*> m_effectSettings;
    KWinTouchScreenData* m_data;

    enum EffectActions {
        PresentWindowsAll = static_cast<int>(
            como::win::electric_border_action::count), // Start at the end of built in actions
        PresentWindowsCurrent,
        PresentWindowsClass,
        Cube,
        Cylinder,
        Sphere,
        Overview,
        Grid,
        TabBox,
        TabBoxAlternative,
        EffectCount
    };

    void monitorInit();
    void monitorLoadSettings();
    void monitorLoadDefaultSettings();
    void monitorSaveSettings();
    void monitorShowEvent();

    static int electricBorderActionFromString(const QString& string);
    static QString electricBorderActionToString(int action);
};

} // namespace

#endif
