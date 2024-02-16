/*
    SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWINCOMPOSITINGDATA_H
#define KWINCOMPOSITINGDATA_H

#include <QObject>

#include "kcmoduledata.h"

class KWinCompositingSetting;

class KWinCompositingData : public KCModuleData
{
    Q_OBJECT

public:
    explicit KWinCompositingData(QObject* parent);

    bool isDefaults() const override;

private:
    KWinCompositingSetting* m_settings;
};

#endif // KWINCOMPOSITINGDATA_H
