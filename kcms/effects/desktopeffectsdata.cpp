/*
    SPDX-FileCopyrightText: 2021 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "desktopeffectsdata.h"
#include "effectsmodel.h"

namespace theseus_ship
{

DesktopEffectsData::DesktopEffectsData(QObject* parent)
    : KCModuleData(parent)
    , m_model(new EffectsModel(this))

{
    disconnect(this, &KCModuleData::aboutToLoad, nullptr, nullptr);
    connect(m_model, &EffectsModel::loaded, this, &KCModuleData::loaded);

    m_model->load();
}

DesktopEffectsData::~DesktopEffectsData()
{
}

bool DesktopEffectsData::isDefaults() const
{
    return m_model->isDefaults();
}

}
