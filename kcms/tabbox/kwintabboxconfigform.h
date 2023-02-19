/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __KWINTABBOXCONFIGFORM_H__
#define __KWINTABBOXCONFIGFORM_H__

#include <QWidget>
#include <QStandardItemModel>

#include "win/tabbox/tabbox_config.h"

class KShortcutsEditor;
class KActionCollection;

namespace Ui
{
class KWinTabBoxConfigForm;
}

namespace KWin
{

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

    explicit KWinTabBoxConfigForm(TabboxType type, QWidget *parent = nullptr);
    ~KWinTabBoxConfigForm() override;

    bool highlightWindows() const;
    bool showTabBox() const;
    int filterScreen() const;
    int filterDesktop() const;
    int filterMinimization() const;
    int applicationMode() const;
    int showDesktopMode() const;
    int switchingMode() const;
    QString layoutName() const;

    void setFilterScreen(win::tabbox_config::ClientMultiScreenMode mode);
    void setFilterDesktop(win::tabbox_config::ClientDesktopMode mode);
    void setFilterMinimization(win::tabbox_config::ClientMinimizedMode mode);
    void setApplicationMode(win::tabbox_config::ClientApplicationsMode mode);
    void setShowDesktopMode(win::tabbox_config::ShowDesktopMode mode);
    void setSwitchingModeChanged(win::tabbox_config::ClientSwitchingMode mode);
    void setLayoutName(const QString &layoutName);

    // EffectCombo Data Model
    void setEffectComboModel(QStandardItemModel *model);
    QVariant effectComboCurrentData(int role = Qt::UserRole) const;

    void loadShortcuts();
    void resetShortcuts();
    void saveShortcuts();
    bool isShortcutsChanged() const;
    bool isShortcutsDefault() const;

    void setHighlightWindowsEnabled(bool enabled);
    void setFilterScreenEnabled(bool enabled);
    void setFilterDesktopEnabled(bool enabled);
    void setFilterMinimizationEnabled(bool enabled);
    void setApplicationModeEnabled(bool enabled);
    void setShowDesktopModeEnabled(bool enabled);
    void setSwitchingModeEnabled(bool enabled);
    void setLayoutNameEnabled(bool enabled);

    void setFilterScreenDefaultIndicatorVisible(bool visible);
    void setFilterDesktopDefaultIndicatorVisible(bool visible);
    void setFilterMinimizationDefaultIndicatorVisible(bool visible);
    void setApplicationModeDefaultIndicatorVisible(bool visible);
    void setShowDesktopModeDefaultIndicatorVisible(bool visible);
    void setSwitchingModeDefaultIndicatorVisible(bool visible);
    void setLayoutNameDefaultIndicatorVisible(bool visible);
    void setShortcutsDefaultIndicatorVisible(bool visible);

Q_SIGNALS:
    void filterScreenChanged(int value);
    void filterDesktopChanged(int value);
    void filterMinimizationChanged(int value);
    void applicationModeChanged(int value);
    void showDesktopModeChanged(int value);
    void switchingModeChanged(int value);
    void layoutNameChanged(const QString &layoutName);
    void effectConfigButtonClicked();
    void shortcutChanged();

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

private:
    void setDefaultIndicatorVisible(QWidget *widget, bool visible);

    KActionCollection *m_actionCollection = nullptr;

    bool m_isHighlightWindowsEnabled = true;
    TabboxType m_type;
    Ui::KWinTabBoxConfigForm *ui;
};

} // namespace

#endif
