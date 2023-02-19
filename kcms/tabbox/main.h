/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
};

} // namespace

#endif
