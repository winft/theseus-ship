/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>
SPDX-FileCopyrightText: 2023 Ismael Asensio <isma.af@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __KWINTABBOXCONFIGFORM_H__
#define __KWINTABBOXCONFIGFORM_H__

#include <QWidget>
#include <QStandardItemModel>

#include <como/win/tabbox/tabbox_config.h>

namespace Ui
{
class KWinTabBoxConfigForm;
}

namespace theseus_ship
{

class TabBoxSettings;
class ShortcutSettings;

class KWinTabBoxConfigForm : public QWidget
{
    Q_OBJECT

public:
    enum class TabboxType
    {
        Main,
        Alternative,
    };


    enum EffectComboRole
    {
        LayoutPath = Qt::UserRole + 1,
        AddonEffect, // i.e not builtin effects
    };

    explicit KWinTabBoxConfigForm(TabboxType type, TabBoxSettings *config, ShortcutSettings *shortcutsConfig, QWidget *parent = nullptr);
    ~KWinTabBoxConfigForm() override;

    TabBoxSettings *config() const;
    bool highlightWindows() const;

    void updateUiFromConfig();
    void setDefaultIndicatorVisible(bool visible);

    // EffectCombo Data Model
    void setEffectComboModel(QStandardItemModel *model);
    QVariant effectComboCurrentData(int role = Qt::UserRole) const;

Q_SIGNALS:
    void configChanged();
    void effectConfigButtonClicked();

private Q_SLOTS:
    void tabBoxToggled(bool on);
    void onFilterScreen();
    void onFilterDesktop();
    void onFilterMinimization();
    void onApplicationMode();
    void onShowDesktopMode();
    void onSwitchingMode();
    void onEffectCombo();
    void onShortcutChanged(const QKeySequence &seq);
    void updateDefaultIndicators();

private:
    void setEnabledUi();
    void applyDefaultIndicator(QList<QWidget *> widgets, bool visible);

    // UI property getters
    bool showTabBox() const;
    int filterScreen() const;
    int filterDesktop() const;
    int filterActivities() const;
    int filterMinimization() const;
    int applicationMode() const;
    int showDesktopMode() const;
    int switchingMode() const;
    QString layoutName() const;

    // UI property setters
    void setFilterScreen(como::win::tabbox_config::ClientMultiScreenMode mode);
    void setFilterDesktop(como::win::tabbox_config::ClientDesktopMode mode);
    void setFilterMinimization(como::win::tabbox_config::ClientMinimizedMode mode);
    void setApplicationMode(como::win::tabbox_config::ClientApplicationsMode mode);
    void setShowDesktopMode(como::win::tabbox_config::ShowDesktopMode mode);
    void setSwitchingModeChanged(como::win::tabbox_config::ClientSwitchingMode mode);
    void setLayoutName(const QString &layoutName);

private:
    TabBoxSettings *m_config = nullptr;
    ShortcutSettings *m_shortcuts = nullptr;
    bool m_showDefaultIndicator = false;

    bool m_isHighlightWindowsEnabled = true;
    Ui::KWinTabBoxConfigForm *ui;
};

} // namespace

#endif
