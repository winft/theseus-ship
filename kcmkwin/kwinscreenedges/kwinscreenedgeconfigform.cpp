/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
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

#include "kwinscreenedgeconfigform.h"
#include "ui_main.h"

namespace KWin
{

KWinScreenEdgesConfigForm::KWinScreenEdgesConfigForm(QWidget *parent)
    : KWinScreenEdge(parent)
    , ui(new Ui::KWinScreenEdgesConfigUI)
{
    ui->setupUi(this);

    connect(ui->kcfg_ElectricBorderDelay, qOverload<int>(&QSpinBox::valueChanged), this, &KWinScreenEdgesConfigForm::sanitizeCooldown);

    // Visual feedback of action group conflicts
    connect(ui->kcfg_ElectricBorders, qOverload<int>(&QComboBox::currentIndexChanged), this, &KWinScreenEdgesConfigForm::groupChanged);
    connect(ui->kcfg_ElectricBorderMaximize, &QCheckBox::stateChanged, this, &KWinScreenEdgesConfigForm::groupChanged);
    connect(ui->kcfg_ElectricBorderTiling, &QCheckBox::stateChanged, this, &KWinScreenEdgesConfigForm::groupChanged);

    connect(ui->electricBorderCornerRatioSpin, qOverload<int>(&QSpinBox::valueChanged), this, &KWinScreenEdgesConfigForm::onChanged);
    connect(ui->electricBorderCornerRatioSpin, qOverload<int>(&QSpinBox::valueChanged), this, &KWinScreenEdgesConfigForm::updateDefaultIndicators);
}

KWinScreenEdgesConfigForm::~KWinScreenEdgesConfigForm()
{
    delete ui;
}

void KWinScreenEdgesConfigForm::setElectricBorderCornerRatio(double value)
{
    m_referenceCornerRatio = value;
    ui->electricBorderCornerRatioSpin->setValue(m_referenceCornerRatio * 100.);
}

void KWinScreenEdgesConfigForm::setDefaultElectricBorderCornerRatio(double value)
{
    m_defaultCornerRatio = value;
    updateDefaultIndicators();
}

double KWinScreenEdgesConfigForm::electricBorderCornerRatio() const
{
    return ui->electricBorderCornerRatioSpin->value() / 100.;
}

void KWinScreenEdgesConfigForm::setElectricBorderCornerRatioEnabled(bool enable)
{
    ui->electricBorderCornerRatioSpin->setEnabled(enable);
}

void KWinScreenEdgesConfigForm::reload()
{
    ui->electricBorderCornerRatioSpin->setValue(m_referenceCornerRatio * 100.);
    KWinScreenEdge::reload();
}

void KWinScreenEdgesConfigForm::setDefaults()
{
    ui->electricBorderCornerRatioSpin->setValue(m_defaultCornerRatio * 100.);
    KWinScreenEdge::setDefaults();
}

void KWinScreenEdgesConfigForm::setDefaultsIndicatorsVisible(bool visible)
{
    if (m_defaultIndicatorVisible != visible) {
        m_defaultIndicatorVisible = visible;
        updateDefaultIndicators();
    }
}

Monitor *KWinScreenEdgesConfigForm::monitor() const
{
    return ui->monitor;
}

bool KWinScreenEdgesConfigForm::isSaveNeeded() const
{
    return m_referenceCornerRatio != electricBorderCornerRatio();
}

bool KWinScreenEdgesConfigForm::isDefault() const
{
    return m_defaultCornerRatio == electricBorderCornerRatio();
}

void KWinScreenEdgesConfigForm::sanitizeCooldown()
{
    ui->kcfg_ElectricBorderCooldown->setMinimum(ui->kcfg_ElectricBorderDelay->value() + 50);
}

void KWinScreenEdgesConfigForm::groupChanged()
{
    // Monitor conflicts
    bool hide = false;
    if (ui->kcfg_ElectricBorders->currentIndex() == 2) {
        hide = true;
    }
    monitorHideEdge(ElectricTop, hide);
    monitorHideEdge(ElectricRight, hide);
    monitorHideEdge(ElectricBottom, hide);
    monitorHideEdge(ElectricLeft, hide);
}

void KWinScreenEdgesConfigForm::updateDefaultIndicators()
{
    ui->electricBorderCornerRatioSpin->setProperty("_kde_highlight_neutral", m_defaultIndicatorVisible && (electricBorderCornerRatio() != m_defaultCornerRatio));
    ui->electricBorderCornerRatioSpin->update();
}

} // namespace
