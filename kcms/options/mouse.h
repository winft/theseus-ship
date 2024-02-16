/*
    mouse.h

    SPDX-FileCopyrightText: 1998 Matthias Ettrich <ettrich@kde.org>


    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __KKWMMOUSECONFIG_H__
#define __KKWMMOUSECONFIG_H__

class KConfig;

#include <KCModule>
#include <KLocalizedString>

#include "ui_actions.h"
#include "ui_mouse.h"

class KWinOptionsSettings;

class KWinMouseConfigForm : public QWidget, public Ui::KWinMouseConfigForm
{
    Q_OBJECT

public:
    explicit KWinMouseConfigForm(QWidget* parent);
};

class KWinActionsConfigForm : public QWidget, public Ui::KWinActionsConfigForm
{
    Q_OBJECT

public:
    explicit KWinActionsConfigForm(QWidget* parent);
};

class KTitleBarActionsConfig : public KCModule
{
    Q_OBJECT

public:
    KTitleBarActionsConfig(bool _standAlone, KWinOptionsSettings* settings, QWidget* parent);

    void save() override;

    bool isDefaults() const;
    bool isSaveNeeded() const;

protected:
    void initialize(KWinOptionsSettings* settings);

private:
    bool standAlone;

    KWinMouseConfigForm* m_ui;
    KWinOptionsSettings* m_settings;
};

class KWindowActionsConfig : public KCModule
{
    Q_OBJECT

public:
    KWindowActionsConfig(bool _standAlone, KWinOptionsSettings* settings, QWidget* parent);

    void save() override;

    bool isDefaults() const;
    bool isSaveNeeded() const;

protected:
    void initialize(KWinOptionsSettings* settings);

private:
    bool standAlone;

    KWinActionsConfigForm* m_ui;
    KWinOptionsSettings* m_settings{nullptr};
};

#endif
